/*
 * Droidspaces v6 - High-performance Container Runtime
 *
 * Surgical iptables and ip6tables rule management via the raw IP_TABLES and
 * IP6_TABLES socket APIs. One engine serves both families.
 *
 * Android Safety Contract (NEVER violate these)
 *   • Never flush any chain (would kill Android tethering/hotspot)
 *   • Never change any chain policy
 *   • Never touch rules we did not create
 *   • Only INSERT rules scoped to our bridge/veth and our own subnets
 *   • Always check existence before inserting (fully idempotent)
 *
 * Kernel / API compatibility
 *   • Kernel 3.10+ (Android/Linux)
 *   • Uses getsockopt/setsockopt on an AF_INET or AF_INET6 SOCK_RAW socket
 *   • The raw socket is always tried first. The iptables(8) / ip6tables(8)
 *     binary is the fallback for whatever the kernel rejects, and the only
 *     path for the MSS clamp and port forwards.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <arpa/inet.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_nat.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

/* Address family descriptor
 *
 * ip_tables and ip6_tables are the same machine with a different rule header.
 * The table-level structs (getinfo, get_entries, replace) and the sockopt
 * numbers are identical, so the engine below uses the ipt_ names for both.
 * A rule entry is [family IP header][common tail], which moves target_offset
 * and next_offset and changes the address width. Everything family specific
 * is in this struct and in entry_view(). */

struct xt_family {
  int af;            /* AF_INET or AF_INET6: the raw socket's family */
  int level;         /* IPPROTO_IP or IPPROTO_IPV6: the sockopt level */
  size_t entry_sz;   /* sizeof the family's rule entry */
  size_t off_target; /* offset of target_offset inside an entry */
  size_t off_next;   /* offset of next_offset inside an entry */
  size_t addr_len;   /* 4 or 16 */
  const char *bin;   /* fallback binary */
};

static const struct xt_family xt_v4 = {
    AF_INET,
    IPPROTO_IP,
    sizeof(struct ipt_entry),
    offsetof(struct ipt_entry, target_offset),
    offsetof(struct ipt_entry, next_offset),
    4,
    "iptables",
};

static const struct xt_family xt_v6 = {
    AF_INET6,
    IPPROTO_IPV6,
    sizeof(struct ip6t_entry),
    offsetof(struct ip6t_entry, target_offset),
    offsetof(struct ip6t_entry, next_offset),
    16,
    "ip6tables",
};

static const struct xt_family *xt_family_of(int family) {
  return family == AF_INET6 ? &xt_v6 : &xt_v4;
}

/* The engine relies on the two APIs sharing these layouts and numbers. If a
 * kernel header ever breaks one of them the build stops here, rather than a
 * table replace corrupting someone's firewall. */
#define DS_XT_SAME(name, cond) typedef char ds_xt_same_##name[(cond) ? 1 : -1]
DS_XT_SAME(getinfo, sizeof(struct ipt_getinfo) == sizeof(struct ip6t_getinfo));
DS_XT_SAME(getinfo_size, offsetof(struct ipt_getinfo, size) ==
                             offsetof(struct ip6t_getinfo, size));
DS_XT_SAME(replace, sizeof(struct ipt_replace) == sizeof(struct ip6t_replace));
DS_XT_SAME(replace_counters, offsetof(struct ipt_replace, counters) ==
                                 offsetof(struct ip6t_replace, counters));
DS_XT_SAME(entrytable, offsetof(struct ipt_get_entries, entrytable) ==
                           offsetof(struct ip6t_get_entries, entrytable));
DS_XT_SAME(sockopts, IPT_SO_GET_INFO == IP6T_SO_GET_INFO &&
                         IPT_SO_GET_ENTRIES == IP6T_SO_GET_ENTRIES &&
                         IPT_SO_SET_REPLACE == IP6T_SO_SET_REPLACE);
DS_XT_SAME(inv_dstip, IPT_INV_DSTIP == IP6T_INV_DSTIP);

static unsigned int ent_u16(const void *e, size_t off) {
  uint16_t v;
  memcpy(&v, (const uint8_t *)e + off, sizeof(v));
  return v;
}

static unsigned int ent_next(const struct xt_family *f, const void *e) {
  return ent_u16(e, f->off_next);
}

static unsigned int ent_target_off(const struct xt_family *f, const void *e) {
  return ent_u16(e, f->off_target);
}

/* The match fields of one entry, whichever family it belongs to. This is the
 * only place that knows the two IP header layouts. */
struct xt_ent_view {
  char *iniface, *outiface;
  unsigned char *in_mask, *out_mask;
  void *src, *smsk, *dst, *dmsk;
  uint8_t *invflags;
};

static void entry_view(const struct xt_family *f, void *e,
                       struct xt_ent_view *v) {
  if (f->af == AF_INET6) {
    struct ip6t_ip6 *ip = &((struct ip6t_entry *)e)->ipv6;
    v->iniface = ip->iniface;
    v->outiface = ip->outiface;
    v->in_mask = ip->iniface_mask;
    v->out_mask = ip->outiface_mask;
    v->src = &ip->src;
    v->smsk = &ip->smsk;
    v->dst = &ip->dst;
    v->dmsk = &ip->dmsk;
    v->invflags = &ip->invflags;
  } else {
    struct ipt_ip *ip = &((struct ipt_entry *)e)->ip;
    v->iniface = ip->iniface;
    v->outiface = ip->outiface;
    v->in_mask = ip->iniface_mask;
    v->out_mask = ip->outiface_mask;
    v->src = &ip->src;
    v->smsk = &ip->smsk;
    v->dst = &ip->dst;
    v->dmsk = &ip->dmsk;
    v->invflags = &ip->invflags;
  }
}

/* Read-only walkers hold const blobs; the view is shared with the rule
 * builders, which write through it. */
static void entry_view_ro(const struct xt_family *f, const void *e,
                          struct xt_ent_view *v) {
  entry_view(f, (void *)(uintptr_t)e, v);
}

/* CIDR helpers - parse_cidr is shared with network.c via the public header */

void parse_cidr(const char *cidr, uint32_t *ip_out, uint32_t *mask_out) {
  char buf[64];
  safe_strncpy(buf, cidr, sizeof(buf));

  char *slash = strchr(buf, '/');
  int prefix = 24;
  if (slash) {
    *slash = '\0';
    prefix = atoi(slash + 1);
  }
  if (prefix < 0)
    prefix = 0;
  if (prefix > 32)
    prefix = 32;

  *ip_out = inet_addr(buf);
  *mask_out = (prefix == 0) ? 0u : htonl(0xffffffffu << (32 - prefix));
}

/* Either family, into 16-byte buffers of which the first addr_len bytes are
 * used. Returns 0, or -1 for a string that is not an address of this family.
 */
static int fam_parse_cidr(const struct xt_family *f, const char *cidr,
                          uint8_t net[16], uint8_t mask[16]) {
  char buf[64];
  safe_strncpy(buf, cidr, sizeof(buf));

  int prefix = (int)f->addr_len * 8;
  char *slash = strchr(buf, '/');
  if (slash) {
    *slash = '\0';
    prefix = atoi(slash + 1);
  }
  if (prefix < 0 || prefix > (int)f->addr_len * 8)
    return -1;

  memset(net, 0, 16);
  memset(mask, 0, 16);
  if (inet_pton(f->af, buf, net) != 1)
    return -1;
  for (size_t i = 0; i < f->addr_len && prefix > 0; i++, prefix -= 8)
    mask[i] = (prefix >= 8) ? 0xff : (uint8_t)(0xff << (8 - prefix));
  return 0;
}

/* Module loader - best-effort, harmless on built-in or absent modprobe.
 *
 * The kernel loads a table's module by itself the first time the table is
 * asked for, but only once ip_tables / ip6_tables has registered the sockopt,
 * and nothing autoloads those. Loading the table modules pulls them in.
 * Called from get_table() when a table turns out to be missing, so a kernel
 * that has everything built in never pays for the ten forks. */

static int modules_probed = 0;

static void probe_iptables_modules(void) {
  if (modules_probed)
    return;
  modules_probed = 1;

  char *mods[] = {"iptable_nat",
                  "iptable_filter",
                  "iptable_mangle",
                  "ip6table_nat",
                  "ip6table_filter",
                  "ip6table_mangle",
                  "ip_conntrack",
                  "xt_conntrack",
                  "nf_nat",
                  "xt_addrtype", /* required for --dst-type LOCAL DNAT */
                  NULL};
  for (int i = 0; mods[i]; i++) {
    char *a[] = {"modprobe", "-q", mods[i], NULL};
    run_command_quiet(a);
  }
}

/* Binary fallback
 *
 * One rule through iptables(8) or ip6tables(8). `spec` is everything after
 * the chain name. The binary knows nothing about what we inserted earlier, so
 * bin_ensure() checks before it inserts: without that, every container start
 * on a host that has no raw API would stack another copy. */

static int bin_rule(const struct xt_family *f, const char *op,
                    const char *table, const char *chain,
                    const char *const spec[]) {
  const char *head[] = {f->bin, "-t", table, op, chain};
  char *argv[24];
  size_t n = 0;

  for (size_t i = 0; i < sizeof(head) / sizeof(head[0]); i++)
    argv[n++] = (char *)(uintptr_t)head[i];
  for (size_t i = 0; spec[i] && n < sizeof(argv) / sizeof(argv[0]) - 1; i++)
    argv[n++] = (char *)(uintptr_t)spec[i];
  argv[n] = NULL;
  return run_command_quiet(argv);
}

static int bin_ensure(const struct xt_family *f, const char *table,
                      const char *chain, const char *const spec[]) {
  if (bin_rule(f, "-C", table, chain, spec) == 0)
    return 0;
  /* An exit status is positive and every caller tests for < 0, so a refused
   * rule used to read as success and NAT66 was announced with no MASQUERADE. */
  return bin_rule(f, "-I", table, chain, spec) == 0 ? 0 : -1;
}

static const char *const k_mss_spec[] = {
    "-p", "tcp",    "--tcp-flags",         "SYN,RST", "SYN",
    "-j", "TCPMSS", "--clamp-mss-to-pmtu", NULL};

/* Internal: get table info + entries blob via getsockopt
 *
 * Returns 0 on success.  *entries_out points to the allocated
 * ipt_get_entries struct; caller frees it.
 *
 * Convenience macro: ENTRIES_BLOB(base) → pointer to the raw rule bytes
 * inside the ipt_get_entries allocation. */

static int get_table(const struct xt_family *f, int fd, const char *table_name,
                     struct ipt_getinfo *info, unsigned char **entries_out) {
  memset(info, 0, sizeof(*info));
  safe_strncpy(info->name, table_name, sizeof(info->name));
  socklen_t info_len = sizeof(*info);

  while (getsockopt(fd, f->level, IPT_SO_GET_INFO, info, &info_len) < 0) {
    int err = errno;
    /* ENOPROTOOPT: ip_tables itself is not in. ENOENT: this table is not.
     * Load the modules once and ask again. */
    if ((err == ENOENT || err == ENOPROTOOPT) && !modules_probed) {
      probe_iptables_modules();
      continue;
    }
    ds_log("[IPT] %s get_table('%s') GET_INFO failed: %s", f->bin, table_name,
           strerror(err));
    return -err;
  }

  size_t esz = sizeof(struct ipt_get_entries) + info->size;
  struct ipt_get_entries *entries = calloc(1, esz);
  if (!entries)
    return -ENOMEM;

  safe_strncpy(entries->name, table_name, sizeof(entries->name));
  entries->size = info->size;
  socklen_t elen = (socklen_t)esz;

  if (getsockopt(fd, f->level, IPT_SO_GET_ENTRIES, entries, &elen) < 0) {
    int err = errno;
    ds_log("[IPT] %s get_table('%s') GET_ENTRIES failed: %s", f->bin,
           table_name, strerror(err));
    free(entries);
    return -err;
  }

  *entries_out = (unsigned char *)entries;
  return 0;
}

#define ENTRIES_BLOB(base)                                                     \
  ((unsigned char *)((struct ipt_get_entries *)(base))->entrytable)

#define DS_IPT_MATCH_IN 0x01u
#define DS_IPT_MATCH_OUT 0x02u

static int hook_contains_offset(const struct ipt_getinfo *info,
                                unsigned int hook_id, unsigned int offset) {
  if (!info || hook_id >= NF_INET_NUMHOOKS ||
      !(info->valid_hooks & (1u << hook_id)))
    return 0;
  return offset >= info->hook_entry[hook_id] &&
         offset < info->underflow[hook_id];
}

static int iface_empty(const char *iface, const unsigned char *mask) {
  for (int i = 0; i < IFNAMSIZ; i++) {
    if (iface[i] != '\0' || mask[i] != 0)
      return 0;
  }
  return 1;
}

static int iface_exact_match(const char *rule_iface,
                             const unsigned char *rule_mask,
                             const char *iface) {
  size_t len;

  if (!iface || !iface[0])
    return 0;

  len = strnlen(iface, IFNAMSIZ);
  if (len == 0 || len >= IFNAMSIZ)
    return 0;
  if (strncmp(rule_iface, iface, IFNAMSIZ) != 0)
    return 0;

  for (size_t i = 0; i <= len; i++) {
    if (rule_mask[i] != 0xff)
      return 0;
  }
  for (size_t i = len + 1; i < IFNAMSIZ; i++) {
    if (rule_mask[i] != 0)
      return 0;
  }
  return 1;
}

static int target_is_accept(const struct xt_entry_target *t) {
  if (strcmp(t->u.user.name, "ACCEPT") == 0)
    return 1;
  if (t->u.user.name[0] == '\0' &&
      t->u.target_size >= XT_ALIGN(sizeof(struct xt_standard_target))) {
    const struct xt_standard_target *st = (const struct xt_standard_target *)t;
    return st->verdict == -NF_ACCEPT - 1;
  }
  return 0;
}

/* Human-readable target name for logging.  Standard targets (ACCEPT and the
 * other built-in verdicts) carry an empty user.name - the verdict is encoded
 * numerically - so decode the common ACCEPT case instead of printing ''. */
static const char *target_label(const struct xt_entry_target *t) {
  if (t->u.user.name[0])
    return t->u.user.name;
  if (target_is_accept(t))
    return "ACCEPT";
  return "standard";
}

/* Internal: walk the blob to find an existing rule matching our fingerprint.
 *
 * All non-NULL criteria must match simultaneously.  src and src_mask point at
 * addr_len bytes, or are NULL for "any source". */

static int rule_exists_in_hook(const struct xt_family *f,
                               const struct ipt_getinfo *info,
                               const unsigned char *blob, unsigned int hook_id,
                               const char *iface_in, const char *iface_out,
                               const void *src, const void *src_mask,
                               const char *target_name) {
  if (!info || !blob || hook_id >= NF_INET_NUMHOOKS ||
      !(info->valid_hooks & (1u << hook_id)))
    return 0;

  unsigned int off = info->hook_entry[hook_id];
  unsigned int end = info->underflow[hook_id];
  if (off > end || end > info->size)
    return 0;

  while (off + f->entry_sz <= end) {
    const unsigned char *e = blob + off;
    unsigned int next = ent_next(f, e);
    unsigned int toff = ent_target_off(f, e);
    if (next < f->entry_sz || next > end - off ||
        toff + sizeof(struct xt_entry_target) > next)
      break;

    const struct xt_entry_target *t =
        (const struct xt_entry_target *)(e + toff);
    struct xt_ent_view v;
    entry_view_ro(f, e, &v);

    int match = 1;

    if (target_name && target_name[0]) {
      if (strcmp(target_name, "ACCEPT") == 0) {
        if (!target_is_accept(t))
          match = 0;
      } else if (strcmp(t->u.user.name, target_name) != 0) {
        match = 0;
      }
    }
    if (match && iface_in && iface_in[0] &&
        strncmp(v.iniface, iface_in, IFNAMSIZ) != 0)
      match = 0;
    if (match && iface_out && iface_out[0] &&
        strncmp(v.outiface, iface_out, IFNAMSIZ) != 0)
      match = 0;
    if (match && src &&
        (memcmp(v.src, src, f->addr_len) != 0 ||
         memcmp(v.smsk, src_mask, f->addr_len) != 0))
      match = 0;

    if (match)
      return 1;
    off += next;
  }
  return 0;
}

/* The standard target of an entry, or NULL when the entry is malformed or its
 * target is an extension. Jump fixups only ever touch standard targets. */
static struct xt_standard_target *
entry_standard_target(const struct xt_family *f, unsigned char *e,
                      unsigned int next) {
  unsigned int toff = ent_target_off(f, e);
  if (toff + sizeof(struct xt_standard_target) > next)
    return NULL;

  struct xt_entry_target *t = (struct xt_entry_target *)(e + toff);
  if (t->u.user.name[0] != '\0' ||
      t->u.target_size != (uint16_t)XT_ALIGN(sizeof(struct xt_standard_target)))
    return NULL;
  return (struct xt_standard_target *)t;
}

/* Internal: fixup_jump_targets
 *
 * After inserting new_rule_sz bytes at insert_off, any xt_standard_target
 * with a positive verdict (= absolute byte offset = chain jump) that pointed
 * to an entry AT OR AFTER insert_off must be incremented by new_rule_sz. */

static void fixup_jump_targets(const struct xt_family *f, unsigned char *blob,
                               unsigned int blob_sz, unsigned int insert_off,
                               unsigned int delta) {
  unsigned int off = 0;

  while (off < blob_sz) {
    unsigned char *e = blob + off;
    unsigned int next = ent_next(f, e);

    if (next < f->entry_sz || off + next > blob_sz)
      break;

    struct xt_standard_target *st = entry_standard_target(f, e, next);
    if (st && st->verdict >= (int)insert_off)
      st->verdict += (int)delta;

    off += next;
  }
}

/* Internal: fixup_jump_targets_removed
 *
 * Removal twin of fixup_jump_targets.  After rules are deleted the surviving
 * entries shift to lower offsets, but every xt_standard_target with a
 * non-negative verdict (= absolute byte offset = chain jump) still holds its
 * OLD offset.  Re-base each by the number of bytes removed before that offset:
 * old_offsets[k] gives an entry's old start, removed_before[k] the bytes
 * removed before it.  Without this the kernel's mark_source_chains() validator
 * can no longer resolve the shifted jump (xt_find_jump_offset fails) and
 * rejects the whole table replace with ELOOP. */

static void fixup_jump_targets_removed(const struct xt_family *f,
                                       unsigned char *blob,
                                       unsigned int blob_sz,
                                       const unsigned int *old_offsets,
                                       const unsigned int *removed_before,
                                       unsigned int nents) {
  unsigned int off = 0;

  while (off < blob_sz) {
    unsigned char *e = blob + off;
    unsigned int next = ent_next(f, e);

    if (next < f->entry_sz || off + next > blob_sz)
      break;

    struct xt_standard_target *st = entry_standard_target(f, e, next);
    if (st && st->verdict >= 0) {
      /* Jump target: find the tracked entry that started at this old
       * offset and subtract the bytes removed before it. */
      for (unsigned int k = 0; k < nents; k++) {
        if (old_offsets[k] == (unsigned int)st->verdict) {
          st->verdict -= (int)removed_before[k];
          break;
        }
      }
    }

    off += next;
  }
}

/* Internal: insert_rule_at_hook
 *
 * Inserts new_rule at the very beginning of the given hook's chain. */

static int insert_rule_at_hook(const struct xt_family *f, int fd,
                               const char *table_name,
                               struct ipt_getinfo *info_in,
                               unsigned char *blob_in, unsigned int hook_id,
                               const void *new_rule, unsigned int new_rule_sz) {
  /*
   * cur_info / cur_blob track the table state we are working from.
   * They start as the caller-supplied values.  On EAGAIN we refetch the table
   * and update these so the next attempt uses a fresh snapshot.
   *
   * cur_base: the allocation returned by get_table() on a refetch.
   *           NULL means we are still using the caller-owned blob_in.
   *           Non-NULL means we own it and must free it on exit.
   */
  struct ipt_getinfo cur_info = *info_in;
  unsigned char *cur_base = NULL; /* NULL → caller owns initial blob */
  unsigned char *cur_blob = blob_in;

  int max_retries = 5; /* generous: handles bursts of netd activity */
  int ret = -1, err = EAGAIN;

  ds_log("[IPT] insert_rule_at_hook: %s table='%s' hook=%u rule_sz=%u", f->bin,
         table_name, hook_id, new_rule_sz);

  while (max_retries-- > 0) {
    unsigned int insert_off = cur_info.hook_entry[hook_id];
    unsigned int old_sz = cur_info.size;
    unsigned int new_sz = old_sz + new_rule_sz;

    ds_log("[IPT] insert_rule_at_hook: table='%s' hook=%u insert_off=%u "
           "old_sz=%u new_sz=%u",
           table_name, hook_id, insert_off, old_sz, new_sz);

    size_t replace_sz = sizeof(struct ipt_replace) + new_sz;
    struct ipt_replace *repl = calloc(1, replace_sz);
    if (!repl) {
      err = ENOMEM;
      break;
    }

    safe_strncpy(repl->name, table_name, sizeof(repl->name));
    repl->valid_hooks = cur_info.valid_hooks;
    repl->num_entries = cur_info.num_entries + 1;
    repl->size = new_sz;

    /* num_counters = OLD count */
    repl->num_counters = cur_info.num_entries;

    /* Counters buffer: kernel writes OLD entry counters back into this array.
     * Must hold cur_info.num_entries (the live count) values, not
     * num_entries+1. */
    repl->counters = calloc(cur_info.num_entries ? cur_info.num_entries : 1,
                            sizeof(struct xt_counters));
    if (!repl->counters) {
      free(repl);
      err = ENOMEM;
      break;
    }

    /* hook_entry/underflow offset adjustment */
    for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
      if (!(cur_info.valid_hooks & (1u << h)))
        continue;

      repl->hook_entry[h] = cur_info.hook_entry[h];
      repl->underflow[h] = cur_info.underflow[h];

      /* hook_entry: strictly greater - the chain we insert INTO keeps its start
       */
      if (cur_info.hook_entry[h] > insert_off)
        repl->hook_entry[h] += new_rule_sz;

      /* underflow: at-or-after - terminal entries always shift */
      if (cur_info.underflow[h] >= insert_off)
        repl->underflow[h] += new_rule_sz;

      ds_log("[IPT]   hook[%d]: entry %u→%u  underflow %u→%u", h,
             cur_info.hook_entry[h], repl->hook_entry[h], cur_info.underflow[h],
             repl->underflow[h]);
    }

    /* Build new blob: prefix | new_rule | suffix */
    unsigned char *nb = (unsigned char *)(repl + 1);
    if (insert_off > 0)
      memcpy(nb, cur_blob, insert_off);
    memcpy(nb + insert_off, new_rule, new_rule_sz);
    if (old_sz > insert_off)
      memcpy(nb + insert_off + new_rule_sz, cur_blob + insert_off,
             old_sz - insert_off);

    /* Patch stale jump verdicts in the shifted suffix */
    fixup_jump_targets(f, nb, new_sz, insert_off, new_rule_sz);

    ret = setsockopt(fd, f->level, IPT_SO_SET_REPLACE, repl,
                     (socklen_t)replace_sz);
    err = errno;

    free(repl->counters);
    free(repl);

    if (ret == 0)
      break;

    if (err == EAGAIN && max_retries > 0) {
      ds_log("[IPT]   EAGAIN (attempt remaining=%d), refetching '%s' table",
             max_retries, table_name);
      usleep(5000 + 10000 * (4 - max_retries)); /* 5 / 15 / 25 / 35 ms */

      /* Free any previously refetched base and get a fresh snapshot. */
      if (cur_base) {
        free(cur_base);
        cur_base = NULL;
      }

      struct ipt_getinfo new_info;
      unsigned char *new_base = NULL;
      if (get_table(f, fd, table_name, &new_info, &new_base) < 0) {
        ds_log("[IPT]   refetch of '%s' failed, giving up", table_name);
        ret = -1;
        err = EAGAIN;
        break;
      }

      cur_base = new_base;
      cur_blob = ENTRIES_BLOB(new_base);
      cur_info = new_info;
      continue;
    }

    break; /* non-EAGAIN error, or last attempt exhausted */
  }

  if (cur_base)
    free(cur_base);

  if (ret < 0) {
    if (err != ENOENT && err != EAGAIN)
      ds_log("[IPT] insert_rule_at_hook: kernel rejected blob (errno=%d %s)",
             err, strerror(err));
    return -err;
  }
  return 0;
}

/* Internal: remove_matching_rules
 *
 * Builds a new blob that omits every entry matching any of our fingerprints,
 * then submits it atomically via IPT_SO_SET_REPLACE.
 *
 * Two-pass algorithm avoids in-place mutation of the blob being walked.
 *
 * match_src / match_mask point at addr_len bytes, or are NULL when only the
 * interface rules are wanted.
 *
 * Returns the number of rules removed (0 when nothing matched), or a negative
 * errno when the table replace failed. */

static int remove_matching_rules(const struct xt_family *f, int fd,
                                 const char *table_name,
                                 struct ipt_getinfo *info_in,
                                 unsigned char *blob_in, unsigned int hook_id,
                                 const void *match_src, const void *match_mask,
                                 const char *match_iface,
                                 unsigned int iface_flags) {
  if (!info_in || !blob_in || hook_id >= NF_INET_NUMHOOKS ||
      !(info_in->valid_hooks & (1u << hook_id)))
    return 0;

  /*
   * cur_info / cur_blob track the table state we are working from.
   * They start as the caller-supplied values.  On EAGAIN we refetch the table
   * and update these so the next attempt uses a fresh snapshot of the rules.
   *
   * cur_base: the allocation returned by get_table() on a refetch.
   *           NULL means we are still using the caller-owned blob_in.
   *           Non-NULL means we own it and must free it on exit.
   */
  struct ipt_getinfo cur_info = *info_in;
  unsigned char *cur_base = NULL; /* NULL → caller owns initial blob */
  unsigned char *cur_blob = blob_in;

  int max_retries = 5;
  int ret = 0, err = 0;
  int removed = 0;

  while (max_retries-- > 0) {
    unsigned char *new_blob = malloc(cur_info.size);
    if (!new_blob) {
      /* On an EAGAIN refetch iteration cur_base owns a get_table() blob; free
       * it before bailing so the early return does not leak it. */
      if (cur_base)
        free(cur_base);
      return -ENOMEM;
    }

    /* Allocate per-entry tracking arrays */
    unsigned int *old_offsets =
        calloc(cur_info.num_entries + 1, sizeof(unsigned int));
    unsigned int *removed_before =
        calloc(cur_info.num_entries + 1, sizeof(unsigned int));

    if (!old_offsets || !removed_before) {
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = -1;
      err = ENOMEM;
      break;
    }

    unsigned int new_sz = 0;
    unsigned int removed_count = 0;
    unsigned int cumulative_gone = 0;
    unsigned int offset = 0;
    unsigned int ei = 0;

    /* Pass 1: walk, classify, build new_blob */
    while (offset + f->entry_sz <= cur_info.size && ei < cur_info.num_entries) {
      const unsigned char *e = cur_blob + offset;
      unsigned int next = ent_next(f, e);
      unsigned int toff = ent_target_off(f, e);
      if (next < f->entry_sz || next > cur_info.size - offset ||
          toff + sizeof(struct xt_entry_target) > next)
        break;

      old_offsets[ei] = offset;
      removed_before[ei] = cumulative_gone;

      const struct xt_entry_target *t =
          (const struct xt_entry_target *)(e + toff);
      const char *tname = t->u.user.name;
      struct xt_ent_view v;
      entry_view_ro(f, e, &v);

      int is_ours = 0;

      if (!hook_contains_offset(&cur_info, hook_id, offset))
        goto keep_rule;

      /* MASQUERADE for our subnet */
      if (match_src && strcmp(tname, "MASQUERADE") == 0 &&
          memcmp(v.src, match_src, f->addr_len) == 0 &&
          memcmp(v.smsk, match_mask, f->addr_len) == 0 &&
          memcmp(v.dst, match_src, f->addr_len) == 0 &&
          memcmp(v.dmsk, match_mask, f->addr_len) == 0 &&
          (*v.invflags & IPT_INV_DSTIP))
        is_ours = 1;

      /* ACCEPT on the exact interface/direction we inserted. */
      if (!is_ours && match_iface && match_iface[0] && target_is_accept(t)) {
        if ((iface_flags & DS_IPT_MATCH_IN) &&
            iface_exact_match(v.iniface, v.in_mask, match_iface) &&
            iface_empty(v.outiface, v.out_mask))
          is_ours = 1;
        if ((iface_flags & DS_IPT_MATCH_OUT) &&
            iface_exact_match(v.outiface, v.out_mask, match_iface) &&
            iface_empty(v.iniface, v.in_mask))
          is_ours = 1;
      }

    keep_rule:
      if (is_ours) {
        /* Safety: never remove an underflow (chain policy) entry. */
        int is_underflow = 0;
        for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
          if ((cur_info.valid_hooks & (1u << h)) &&
              offset == cur_info.underflow[h]) {
            is_underflow = 1;
            break;
          }
        }
        if (is_underflow) {
          ds_warn("[IPT] remove: would remove underflow entry at offset %u - "
                  "skipping",
                  offset);
          memcpy(new_blob + new_sz, e, next);
          new_sz += next;
          offset += next;
          ei++;
          continue;
        }
        ds_log("[IPT] remove: matched %s '%s' rule at offset %u", f->bin,
               target_label(t), offset);
        cumulative_gone += next;
        removed_count++;
      } else {
        memcpy(new_blob + new_sz, e, next);
        new_sz += next;
      }

      offset += next;
      ei++;
    }
    old_offsets[ei] = offset;
    removed_before[ei] = cumulative_gone;

    if (removed_count == 0) {
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = 0; /* nothing to do */
      break;
    }

    /* Re-base surviving jump verdicts for the bytes just removed; without this
     * the kernel rejects the table replace with ELOOP (see
     * fixup_jump_targets_removed). */
    fixup_jump_targets_removed(f, new_blob, new_sz, old_offsets, removed_before,
                               ei + 1);

    /* Build ipt_replace */
    size_t replace_sz = sizeof(struct ipt_replace) + new_sz;
    struct ipt_replace *repl = calloc(1, replace_sz);
    if (!repl) {
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = -1;
      err = ENOMEM;
      break;
    }

    safe_strncpy(repl->name, table_name, sizeof(repl->name));
    repl->valid_hooks = cur_info.valid_hooks;
    repl->num_entries = cur_info.num_entries - removed_count;
    repl->size = new_sz;

    /* num_counters: must be the OLD count */
    repl->num_counters = cur_info.num_entries;
    repl->counters = calloc(cur_info.num_entries ? cur_info.num_entries : 1,
                            sizeof(struct xt_counters));

    if (!repl->counters) {
      free(repl);
      free(new_blob);
      free(old_offsets);
      free(removed_before);
      ret = -1;
      err = ENOMEM;
      break;
    }

    /* Pass 2: fix up hook_entry / underflow offsets */
    for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
      if (!(cur_info.valid_hooks & (1u << h)))
        continue;

      unsigned int adj = 0;
      for (unsigned int k = 0; k <= ei; k++) {
        if (old_offsets[k] >= cur_info.hook_entry[h]) {
          adj = removed_before[k];
          break;
        }
      }
      repl->hook_entry[h] = cur_info.hook_entry[h] - adj;

      adj = 0;
      for (unsigned int k = 0; k <= ei; k++) {
        if (old_offsets[k] >= cur_info.underflow[h]) {
          adj = removed_before[k];
          break;
        }
      }
      repl->underflow[h] = cur_info.underflow[h] - adj;
    }

    memcpy(repl + 1, new_blob, new_sz);

    ret = setsockopt(fd, f->level, IPT_SO_SET_REPLACE, repl,
                     (socklen_t)replace_sz);
    err = errno;

    free(repl->counters);
    free(repl);
    free(new_blob);
    free(old_offsets);
    free(removed_before);

    if (ret == 0) {
      removed = (int)removed_count;
      break;
    }

    if (err == EAGAIN && max_retries > 0) {
      ds_log("[IPT] remove: EAGAIN (attempt remaining=%d), refetching '%s'",
             max_retries, table_name);
      usleep(5000 + 10000 * (4 - max_retries));

      if (cur_base) {
        free(cur_base);
        cur_base = NULL;
      }

      struct ipt_getinfo new_info;
      unsigned char *new_base = NULL;
      if (get_table(f, fd, table_name, &new_info, &new_base) < 0) {
        ds_log("[IPT] remove: refetch of '%s' failed, giving up", table_name);
        ret = -1;
        err = EAGAIN;
        break;
      }

      cur_base = new_base;
      cur_blob = ENTRIES_BLOB(new_base);
      cur_info = new_info;
      continue;
    }

    ds_warn("[IPT] remove: table replace on '%s' failed (%s) - falling back "
            "to the %s binary",
            table_name, strerror(err), f->bin);
    break;
  }

  if (cur_base)
    free(cur_base);

  return (ret < 0) ? -err : removed;
}

/* Internal: open a raw socket (shared across public APIs) */

static int open_raw_socket(const struct xt_family *f) {
  int fd = socket(f->af, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_RAW);
  if (fd < 0)
    ds_log("[IPT] Failed to open %s raw socket: %s", f->bin, strerror(errno));
  return fd;
}

/* Start a rule in buf: zeroed, offsets set for a target of target_sz bytes.
 * Returns the target slot. buf must hold XT_ALIGN(entry_sz) + target_sz. */
static struct xt_entry_target *
rule_begin(const struct xt_family *f, unsigned char *buf, size_t target_sz) {
  size_t hdr = XT_ALIGN(f->entry_sz);
  uint16_t toff = (uint16_t)hdr, next = (uint16_t)(hdr + target_sz);

  memset(buf, 0, hdr + target_sz);
  memcpy(buf + f->off_target, &toff, sizeof(toff));
  memcpy(buf + f->off_next, &next, sizeof(next));
  return (struct xt_entry_target *)(buf + hdr);
}

/* Big enough for any rule we build, in either family. */
#define DS_XT_RULE_MAX                                                         \
  (XT_ALIGN(sizeof(struct ip6t_entry)) +                                       \
   XT_ALIGN(sizeof(struct xt_entry_target) + sizeof(struct nf_nat_range)))

/* Public API: ds_ipt_host_rules_present
 *
 * Fork-free probe for the whole host-side rule set of one family:
 *   filter INPUT       -i <iface> ACCEPT
 *   filter FORWARD     -i <iface> ACCEPT
 *   filter FORWARD     -o <iface> ACCEPT
 *   nat    POSTROUTING -s <cidr> ! -d <cidr> MASQUERADE
 *   nat    PREROUTING  DNAT                     (only if expect_dnat)
 *   mangle POSTROUTING TCPMSS
 *
 * The route monitor polls this as its "have our rules been removed?" signal,
 * so it must stay cheap: one getsockopt pair per table, no iptables binary.
 * Checking every rule rather than one canary matters because a netd restart is
 * not the only thing that removes them - firewall apps rewrite the filter
 * chains while leaving nat's MASQUERADE untouched, which a MASQUERADE-only
 * probe would never notice.
 *
 * Two deliberate limits, both bounded by what the entry blob exposes without
 * parsing xt match payloads:
 *   - The DNAT and TCPMSS checks match on target name, so they prove that
 *     port forwarding / MSS clamping is still installed, not that every
 *     individual --port mapping is.  A single mapping deleted on its own is
 *     not detected; anything that removes them as a group is.
 *   - An unreadable mangle table is not treated as failure.  MSS clamping is
 *     an MTU guard rather than connectivity, and failing the whole probe there
 *     would stop us reconciling the rules that do matter.
 *
 * Returns 1 when all of them are present, 0 when any is missing, and -1 when
 * the filter or nat table could not be read.  Callers must treat -1 as
 * "unknown" and do nothing: on a kernel with no raw API the rules live
 * wherever the binary put them, and this probe cannot see them. */

int ds_ipt_host_rules_present(int family, const char *iface,
                              const char *src_cidr, int expect_dnat) {
  const struct xt_family *f = xt_family_of(family);
  uint8_t net[16], mask[16];
  if (fam_parse_cidr(f, src_cidr, net, mask) < 0)
    return -1;

  int fd = open_raw_socket(f);
  if (fd < 0)
    return -1;

  struct ipt_getinfo info;
  unsigned char *base = NULL;

  /* filter: the three iface ACCEPT rules, in one table read. */
  if (get_table(f, fd, "filter", &info, &base) < 0) {
    close(fd);
    return -1;
  }
  int present =
      rule_exists_in_hook(f, &info, ENTRIES_BLOB(base), NF_INET_LOCAL_IN, iface,
                          NULL, NULL, NULL, "ACCEPT") &&
      rule_exists_in_hook(f, &info, ENTRIES_BLOB(base), NF_INET_FORWARD, iface,
                          NULL, NULL, NULL, "ACCEPT") &&
      rule_exists_in_hook(f, &info, ENTRIES_BLOB(base), NF_INET_FORWARD, NULL,
                          iface, NULL, NULL, "ACCEPT");
  free(base);

  /* nat: MASQUERADE, plus a port-forward DNAT when --port is configured.
   * Skipped once something is already known missing - the caller reinstalls
   * the full set either way. */
  if (present) {
    base = NULL;
    if (get_table(f, fd, "nat", &info, &base) < 0) {
      close(fd);
      return -1;
    }
    present =
        rule_exists_in_hook(f, &info, ENTRIES_BLOB(base), NF_INET_POST_ROUTING,
                            NULL, NULL, net, mask, "MASQUERADE");
    if (present && expect_dnat)
      present =
          rule_exists_in_hook(f, &info, ENTRIES_BLOB(base), NF_INET_PRE_ROUTING,
                              NULL, NULL, NULL, NULL, "DNAT");
    free(base);
  }

  /* mangle: the MSS clamp.  An unreadable mangle table leaves the verdict
   * untouched rather than failing the probe - see the header comment. */
  if (present) {
    base = NULL;
    if (get_table(f, fd, "mangle", &info, &base) == 0) {
      present = rule_exists_in_hook(f, &info, ENTRIES_BLOB(base),
                                    NF_INET_POST_ROUTING, NULL, NULL, NULL,
                                    NULL, "TCPMSS");
      free(base);
    }
  }

  close(fd);
  return present ? 1 : 0;
}

/* Public API: ds_ipt6_available
 *
 * Can this host do NAT66 at all? That takes two things: the ip6 nat table
 * (CONFIG_IP6_NF_NAT, which stock GKI kernels leave out) and an IPv6
 * MASQUERADE target, which is a separate option on older kernels. The kernel
 * is asked for both, the same way ip6tables resolves a target, so the answer
 * holds whatever the config option is called on this version. Both the runtime
 * and `droidspaces check` come through here. */

int ds_ipt6_available(void) {
  int fd = open_raw_socket(&xt_v6);
  if (fd >= 0) {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    if (get_table(&xt_v6, fd, "nat", &info, &base) == 0) {
      free(base);
      /* Revision 0 is the one raw_ensure_masquerade() inserts. */
      struct xt_get_revision rev = {.name = "MASQUERADE"};
      socklen_t len = sizeof(rev);
      int ret =
          getsockopt(fd, xt_v6.level, IP6T_SO_GET_REVISION_TARGET, &rev, &len);
      close(fd);
      return ret >= 0;
    }
    close(fd);
  }

  /* No raw API (nftables only kernel), so only the binary can ask. Listing
   * the table would not load the target, so append the real thing to a chain
   * nothing jumps to and take it away again. No packet ever sees it. */
  const char *const none[] = {NULL};
  const char *const masq[] = {"-j", "MASQUERADE", NULL};
  bin_rule(&xt_v6, "-N", "nat", "ds-cap-masq", none);
  int ok = bin_rule(&xt_v6, "-A", "nat", "ds-cap-masq", masq) == 0;
  bin_rule(&xt_v6, "-F", "nat", "ds-cap-masq", none);
  bin_rule(&xt_v6, "-X", "nat", "ds-cap-masq", none);
  return ok;
}

/* Internal: raw_ensure_masquerade
 *
 * Returns 0 when the rule is in the nat table (already or newly), or a
 * negative errno when the raw path could not put it there. */

static int raw_ensure_masquerade(const struct xt_family *f, const uint8_t *net,
                                 const uint8_t *mask) {
  int fd = open_raw_socket(f);
  if (fd < 0)
    return -ENOPROTOOPT;

  struct ipt_getinfo info;
  unsigned char *base = NULL;
  int ret = get_table(f, fd, "nat", &info, &base);
  if (ret < 0) {
    close(fd);
    return ret;
  }

  /* Idempotency check */
  if (rule_exists_in_hook(f, &info, ENTRIES_BLOB(base), NF_INET_POST_ROUTING,
                          NULL, NULL, net, mask, "MASQUERADE")) {
    ds_log("[IPT] %s MASQUERADE already present - skipping", f->bin);
    free(base);
    close(fd);
    return 0;
  }

  /* The kernel validates the target payload size per family: IPv4 wants the
   * old multi-range struct with rangesize=1, IPv6 a plain nf_nat_range. An
   * all-zero range means "pick the outgoing address", which is all we want. */
  size_t payload = (f->af == AF_INET6)
                       ? sizeof(struct nf_nat_range)
                       : sizeof(struct nf_nat_ipv4_multi_range_compat);
  size_t target_sz = XT_ALIGN(sizeof(struct xt_entry_target) + payload);

  unsigned char rule_buf[DS_XT_RULE_MAX];
  struct xt_entry_target *rt = rule_begin(f, rule_buf, target_sz);
  struct xt_ent_view v;
  entry_view(f, rule_buf, &v);

  memcpy(v.src, net, f->addr_len);
  memcpy(v.smsk, mask, f->addr_len);
  memcpy(v.dst, net, f->addr_len);
  memcpy(v.dmsk, mask, f->addr_len);
  *v.invflags = IPT_INV_DSTIP; /* ! -d */

  rt->u.target_size = (__u16)target_sz;
  safe_strncpy(rt->u.user.name, "MASQUERADE", sizeof(rt->u.user.name));
  if (f->af == AF_INET)
    ((struct nf_nat_ipv4_multi_range_compat *)rt->data)->rangesize = 1;

  ret = insert_rule_at_hook(f, fd, "nat", &info, ENTRIES_BLOB(base),
                            NF_INET_POST_ROUTING, rule_buf,
                            (unsigned int)(XT_ALIGN(f->entry_sz) + target_sz));
  free(base);
  close(fd);
  return ret;
}

/* Public API: ds_ipt_ensure_masquerade
 *
 * Inserts: -t nat -I POSTROUTING -s <cidr> ! -d <cidr> -j MASQUERADE */

int ds_ipt_ensure_masquerade(int family, const char *src_cidr) {
  const struct xt_family *f = xt_family_of(family);
  ds_log("[IPT] ensure_masquerade: %s cidr=%s", f->bin, src_cidr);

  uint8_t net[16], mask[16];
  if (fam_parse_cidr(f, src_cidr, net, mask) < 0)
    return -EINVAL;

  int ret = raw_ensure_masquerade(f, net, mask);
  if (ret == 0) {
    ds_log("[IPT] %s MASQUERADE in place via raw socket API", f->bin);
    return 0;
  }

  ds_log("[IPT] %s MASQUERADE raw path failed (ret=%d), using the binary",
         f->bin, ret);
  const char *const spec[] = {"-s",     src_cidr, "!",          "-d",
                              src_cidr, "-j",     "MASQUERADE", NULL};
  return bin_ensure(f, "nat", "POSTROUTING", spec);
}

/* Internal: raw_ensure_iface_accept
 *
 * "-I <chain> [-i|-o] <iface> -j ACCEPT" in the filter table through the raw
 * socket. Returns 0 when the rule is there, a negative errno otherwise. */

static int raw_ensure_iface_accept(const struct xt_family *f, int fd,
                                   unsigned int hook, const char *chain,
                                   const char *iface, int is_out) {
  if (fd < 0)
    return -ENOPROTOOPT;

  struct ipt_getinfo info;
  unsigned char *base = NULL;
  int ret = get_table(f, fd, "filter", &info, &base);
  if (ret < 0)
    return ret;

  const char *want_in = is_out ? NULL : iface;
  const char *want_out = is_out ? iface : NULL;

  if (rule_exists_in_hook(f, &info, ENTRIES_BLOB(base), hook, want_in, want_out,
                          NULL, NULL, "ACCEPT")) {
    ds_log("[IPT] %s %s -%c %s ACCEPT already present", f->bin, chain,
           is_out ? 'o' : 'i', iface);
    free(base);
    return 0;
  }

  size_t target_sz = XT_ALIGN(sizeof(struct xt_standard_target));
  unsigned char rule_buf[DS_XT_RULE_MAX];
  struct xt_standard_target *st =
      (struct xt_standard_target *)rule_begin(f, rule_buf, target_sz);
  struct xt_ent_view v;
  entry_view(f, rule_buf, &v);

  safe_strncpy(is_out ? v.outiface : v.iniface, iface, IFNAMSIZ);
  memset(is_out ? v.out_mask : v.in_mask, 0xff,
         strnlen(iface, IFNAMSIZ - 1) + 1);

  st->target.u.target_size = (__u16)target_sz;
  st->verdict = -NF_ACCEPT - 1;

  ret = insert_rule_at_hook(f, fd, "filter", &info, ENTRIES_BLOB(base), hook,
                            rule_buf,
                            (unsigned int)(XT_ALIGN(f->entry_sz) + target_sz));
  free(base);
  if (ret == 0)
    ds_log("[IPT] %s %s -%c %s ACCEPT inserted via raw socket API", f->bin,
           chain, is_out ? 'o' : 'i', iface);
  return ret;
}

/* Raw first, then the binary for whatever the kernel would not take. */
static int ensure_iface_accept(const struct xt_family *f, int fd,
                               unsigned int hook, const char *chain,
                               const char *iface, int is_out) {
  int ret = raw_ensure_iface_accept(f, fd, hook, chain, iface, is_out);
  if (ret == 0)
    return 0;

  ds_log("[IPT] %s %s -%c %s raw path failed (ret=%d), using the binary",
         f->bin, chain, is_out ? 'o' : 'i', iface, ret);
  const char *const spec[] = {is_out ? "-o" : "-i", iface, "-j", "ACCEPT",
                              NULL};
  return bin_ensure(f, "filter", chain, spec);
}

/* Public API: ds_ipt_ensure_forward_accept
 *
 * Inserts:
 *   -t filter -I FORWARD -i <iface> -j ACCEPT
 *   -t filter -I FORWARD -o <iface> -j ACCEPT
 *
 * Called with DS_NAT_BRIDGE ("ds-br0") when bridge-nf-call-iptables=1 so
 * the FORWARD hook sees the bridge interface name as the ingress/egress. */

int ds_ipt_ensure_forward_accept(int family, const char *iface) {
  const struct xt_family *f = xt_family_of(family);
  ds_log("[IPT] ensure_forward_accept: %s iface=%s", f->bin, iface);

  int fd = open_raw_socket(f);
  int in = ensure_iface_accept(f, fd, NF_INET_FORWARD, "FORWARD", iface, 0);
  int out = ensure_iface_accept(f, fd, NF_INET_FORWARD, "FORWARD", iface, 1);
  if (fd >= 0)
    close(fd);
  return (in == 0 && out == 0) ? 0 : -1;
}

/* Public API: ds_ipt_ensure_input_accept
 *
 * Inserts: -t filter -I INPUT -i <iface> -j ACCEPT */

int ds_ipt_ensure_input_accept(int family, const char *iface) {
  const struct xt_family *f = xt_family_of(family);
  ds_log("[IPT] ensure_input_accept: %s iface=%s", f->bin, iface);

  int fd = open_raw_socket(f);
  int ret = ensure_iface_accept(f, fd, NF_INET_LOCAL_IN, "INPUT", iface, 0);
  if (fd >= 0)
    close(fd);
  return ret;
}

/* Public API: ds_ipt_ensure_mss_clamp
 *
 * MSS clamping rule for TCP SYN packets - prevents MTU blackhole through
 * bridge + veth path. It matters most on IPv6, where routers never fragment
 * and a mobile uplink is often well under the veth's 1500.
 *
 * The raw socket API for this rule is disproportionately complex because it
 * requires two match extension payloads (xt_tcp + xt_TCPMSS with pmtu flag).
 * We use the binary exclusively. On a host without it the clamp is simply
 * absent and path MTU discovery has to do the job. */

int ds_ipt_ensure_mss_clamp(int family) {
  const struct xt_family *f = xt_family_of(family);
  ds_log("[IPT] ensure_mss_clamp: %s", f->bin);
  return bin_ensure(f, "mangle", "POSTROUTING", k_mss_spec);
}

/* Remove one of our rules: raw first, then the binary.
 *
 * The binary also runs when the raw path found nothing. A rule that went in
 * through the binary fallback may live somewhere the raw socket cannot see
 * (iptables-nft keeps its rules in nftables), and skipping the delete there
 * would leak it. Deleting a rule that does not exist fails quietly. */
static void remove_rule(const struct xt_family *f, int fd, const char *table,
                        unsigned int hook, const char *chain, const void *src,
                        const void *mask, const char *iface,
                        unsigned int iface_flags, const char *const spec[]) {
  int removed = -1;

  if (fd >= 0) {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    if (get_table(f, fd, table, &info, &base) == 0) {
      removed = remove_matching_rules(f, fd, table, &info, ENTRIES_BLOB(base),
                                      hook, src, mask, iface, iface_flags);
      free(base);
    }
  }
  if (removed <= 0)
    bin_rule(f, "-D", table, chain, spec);
}

/* The three ACCEPT rules ds_ipt_ensure_{forward,input}_accept put on iface. */
static void remove_iface_accepts(const struct xt_family *f, int fd,
                                 const char *iface) {
  const char *const in_spec[] = {"-i", iface, "-j", "ACCEPT", NULL};
  const char *const out_spec[] = {"-o", iface, "-j", "ACCEPT", NULL};

  remove_rule(f, fd, "filter", NF_INET_FORWARD, "FORWARD", NULL, NULL, iface,
              DS_IPT_MATCH_IN, in_spec);
  remove_rule(f, fd, "filter", NF_INET_FORWARD, "FORWARD", NULL, NULL, iface,
              DS_IPT_MATCH_OUT, out_spec);
  remove_rule(f, fd, "filter", NF_INET_LOCAL_IN, "INPUT", NULL, NULL, iface,
              DS_IPT_MATCH_IN, in_spec);
}

int ds_ipt_remove_iface_rules(int family, const char *iface) {
  if (!iface || !iface[0])
    return 0;

  const struct xt_family *f = xt_family_of(family);
  ds_log("[IPT] remove_iface_rules: %s iface=%s", f->bin, iface);

  int fd = open_raw_socket(f);
  remove_iface_accepts(f, fd, iface);
  if (fd >= 0)
    close(fd);
  return 0;
}

/* Public API: ds_ipt_remove_ds_rules
 *
 * Cleanly removes all rules Droidspaces inserted during NAT setup for one
 * family. Safe to call even if container died unexpectedly, and when that
 * family was never set up. */

int ds_ipt_remove_ds_rules(int family) {
  const struct xt_family *f = xt_family_of(family);
  const char *subnet =
      (family == AF_INET6) ? DS_NAT6_SUBNET : DS_DEFAULT_SUBNET;
  ds_log("[IPT] remove_ds_rules: %s", f->bin);

  uint8_t net[16], mask[16];
  if (fam_parse_cidr(f, subnet, net, mask) < 0)
    return -EINVAL;

  int fd = open_raw_socket(f);

  const char *const masq_spec[] = {"-s",   subnet, "!",          "-d",
                                   subnet, "-j",   "MASQUERADE", NULL};
  remove_rule(f, fd, "nat", NF_INET_POST_ROUTING, "POSTROUTING", net, mask,
              NULL, 0, masq_spec);
  remove_iface_accepts(f, fd, DS_NAT_BRIDGE);

  if (fd >= 0)
    close(fd);

  /* MSS clamp: binary only, like its install. */
  bin_rule(f, "-D", "mangle", "POSTROUTING", k_mss_spec);
  return 0;
}

/* Internal: check if xt_addrtype match is available on this kernel.
 * Reads /proc/net/ip_tables_matches which lists every loaded/built-in match.
 * Falls back to false if the file is unreadable (e.g. no CONFIG_NETFILTER). */

static int addrtype_available(void) {
  /* A match the kernel has not loaded is not listed. Nothing asks the kernel
   * to load one until a rule names it, so load the module batch once before
   * giving up, as the old unconditional batch did. */
  for (int tried = 0;; tried = 1) {
    FILE *f = fopen("/proc/net/ip_tables_matches", "re");
    if (!f)
      return 0;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, "addrtype", 8) == 0) {
        fclose(f);
        return 1;
      }
    }
    fclose(f);
    if (tried || modules_probed)
      return 0;
    probe_iptables_modules();
  }
}

/* Port-forward state file helpers
 *
 * We persist a record of every rule actually inserted into iptables so that
 * ds_ipt_remove_portforwards can delete exactly those rules even when the
 * user edits (or empties) the port-forward list in the container config
 * while the container is running (config-drift).
 *
 * State file path : <workspace>/Net/pf_<container_ip>.state
 * Format          : one line per inserted rule, 5 space-separated fields:
 *   <addrtype|basic> <proto> <host_port_str> <to_dest> <cont_port_str>
 *
 * The file is created/truncated at ds_ipt_add_portforwards() time and
 * unlinked after ds_ipt_remove_portforwards() consumes it. */

static void pf_state_path(const char *container_ip, char *buf, size_t len) {
  snprintf(buf, len, "%s/pf_%s.state", get_net_dir(), container_ip);
}

/* Append one successfully-inserted rule to the state file. */
static void pf_state_append(FILE *f, const char *variant, const char *proto,
                            const char *host_port_str, const char *to_dest,
                            const char *cont_port_str) {
  if (!f)
    return;
  fprintf(f, "%s %s %s %s %s\n", variant, proto, host_port_str, to_dest,
          cont_port_str);
  fflush(f);
}

/* Read the state file and issue iptables -D for every recorded rule.
 * Returns 1 if the state file existed (regardless of delete outcomes),
 * 0 if the file was absent (caller should fall back to other strategies). */
static int pf_state_remove(const char *container_ip) {
  char path[PATH_MAX];
  pf_state_path(container_ip, path, sizeof(path));

  FILE *f = fopen(path, "r");
  if (!f)
    return 0;

  char line[256];
  while (fgets(line, sizeof(line), f)) {
    size_t ll = strlen(line);
    if (ll > 0 && line[ll - 1] == '\n')
      line[ll - 1] = '\0';

    char variant[16], proto[4], host_port_str[16], to_dest[80],
        cont_port_str[16];
    if (sscanf(line, "%15s %3s %15s %79s %15s", variant, proto, host_port_str,
               to_dest, cont_port_str) != 5)
      continue;

    /* Delete PREROUTING DNAT + FORWARD ACCEPT - mirror the variant inserted */
    if (strcmp(variant, "addrtype") == 0) {
      char *del[] = {"iptables",    "-t",         "nat",   "-D",
                     "PREROUTING",  "-p",         proto,   "-m",
                     "addrtype",    "--dst-type", "LOCAL", "--dport",
                     host_port_str, "-j",         "DNAT",  "--to-destination",
                     to_dest,       NULL};
      run_command_quiet(del);

      char cont_ip_buf[INET_ADDRSTRLEN];
      safe_strncpy(cont_ip_buf, container_ip, sizeof(cont_ip_buf));
      char *del_fwd[] = {"iptables",    "-D", "FORWARD",   "-p",
                         proto,         "-d", cont_ip_buf, "--dport",
                         cont_port_str, "-j", "ACCEPT",    NULL};
      run_command_quiet(del_fwd);
    } else if (strcmp(variant, "basic") == 0) {
      char *del[] = {"iptables",    "-t", "nat",  "-D",
                     "PREROUTING",  "-p", proto,  "--dport",
                     host_port_str, "-j", "DNAT", "--to-destination",
                     to_dest,       NULL};
      run_command_quiet(del);

      char cont_ip_buf[INET_ADDRSTRLEN];
      safe_strncpy(cont_ip_buf, container_ip, sizeof(cont_ip_buf));
      char *del_fwd[] = {"iptables",    "-D", "FORWARD",   "-p",
                         proto,         "-d", cont_ip_buf, "--dport",
                         cont_port_str, "-j", "ACCEPT",    NULL};
      run_command_quiet(del_fwd);
    } else if (strcmp(variant, "local_out") == 0) {
      /* Localhost OUTPUT DNAT (127.0.0.1:host_port -> to_dest) */
      char *del[] = {"iptables",    "-t",
                     "nat",         "-D",
                     "OUTPUT",      "-p",
                     proto,         "-o",
                     "lo",          "--dport",
                     host_port_str, "-j",
                     "DNAT",        "--to-destination",
                     to_dest,       NULL};
      run_command_quiet(del);
    } else if (strcmp(variant, "local_post") == 0) {
      /* Localhost POSTROUTING MASQUERADE for return traffic */
      char cont_ip_buf[INET_ADDRSTRLEN];
      safe_strncpy(cont_ip_buf, container_ip, sizeof(cont_ip_buf));
      char *del[] = {"iptables",    "-t",      "nat",         "-D",
                     "POSTROUTING", "-p",      proto,         "-d",
                     cont_ip_buf,   "--dport", cont_port_str, "-j",
                     "MASQUERADE",  NULL};
      run_command_quiet(del);
    }
  }

  fclose(f);
  unlink(path);
  return 1;
}

/* Format the three iptables strings for one port-forward entry.  Range entries
 * (host_port_end set) use START:END for --dport and START-END for
 * --to-destination; single ports use the plain number. */
static void pf_fmt_ports(const struct ds_port_forward *pf,
                         const char *container_ip, char host_port_str[16],
                         char cont_port_str[16], char to_dest[80]) {
  if (pf->host_port_end) {
    snprintf(host_port_str, 16, "%u:%u", pf->host_port, pf->host_port_end);
    snprintf(cont_port_str, 16, "%u:%u", pf->container_port,
             pf->container_port_end);
    snprintf(to_dest, 80, "%s:%u-%u", container_ip, pf->container_port,
             pf->container_port_end);
  } else {
    snprintf(host_port_str, 16, "%u", pf->host_port);
    snprintf(cont_port_str, 16, "%u", pf->container_port);
    snprintf(to_dest, 80, "%s:%u", container_ip, pf->container_port);
  }
}

/* Public API: ds_ipt_add_portforwards
 *
 * For each entry in cfg->port_forwards, inserts:
 *   -t nat    -I PREROUTING  -p <proto> --dport <host_port> -j DNAT
 *             --to-destination <container_ip>:<container_port>
 *   -t filter -I FORWARD     -p <proto> -d <container_ip>
 *             --dport <container_port> -j ACCEPT
 *
 * Binary fallback only - DNAT raw socket construction is disproportionately
 * complex for a feature that only fires at container start. */

int ds_ipt_add_portforwards(struct ds_port_forward *pfs, int count,
                            const char *container_ip) {
  if (!pfs || count <= 0 || !container_ip || container_ip[0] == '\0')
    return 0;

  /* Drop anything a previous install recorded before adding it again.  These
   * rules go in through the iptables binary, which has no idempotency check,
   * so re-running this on a partially flushed table (netd wiped nat
   * PREROUTING but left our POSTROUTING/FORWARD rules, say) would otherwise
   * stack a second copy of every survivor - and removal only issues one -D
   * per recorded rule, so the extras would leak on container stop.  No state
   * file (first install) makes this a no-op. */
  pf_state_remove(container_ip);

  /* Open the state file for this container (truncate any stale copy).
   * Every rule we successfully insert is recorded so that
   * ds_ipt_remove_portforwards can delete exactly those rules even if the
   * port-forward list is edited in the config while the container runs. */
  char state_path[PATH_MAX];
  pf_state_path(container_ip, state_path, sizeof(state_path));
  FILE *state_f = fopen(state_path, "w");
  if (!state_f)
    ds_warn("[IPT] Could not open port-forward state file %s: %s - "
            "cleanup on stop may be incomplete",
            state_path, strerror(errno));

  /* Probe once before the loop - avoids reopening /proc/net/ip_tables_matches
   * for every port forward entry. */
  int use_addrtype = addrtype_available();

  /* Every port-forward rule below also gets a matching localhost DNAT
   * (OUTPUT chain, -o lo) so 127.0.0.1:HOST reaches the container the same
   * way LAN peers do. That requires route_localnet=1 (kernel drops loopback
   * destined/sourced packets otherwise). Enable once here; the route monitor
   * re-asserts it for the process lifetime (Android/netd resets it). */
  write_file("/proc/sys/net/ipv4/conf/all/route_localnet", "1");
  ds_net_mark_local_forward_active();

  for (int i = 0; i < count; i++) {
    struct ds_port_forward *pf = &pfs[i];

    char host_port_str[16], cont_port_str[16], to_dest[80];
    pf_fmt_ports(pf, container_ip, host_port_str, cont_port_str, to_dest);

    ds_log("portforward: %s %s -> %s", pf->proto, host_port_str, to_dest);

    /* PREROUTING DNAT.
     * Preferred: -m addrtype --dst-type LOCAL restricts the rule to traffic
     * destined for the phone itself - prevents hijacking hotspot client flows.
     * Fallback: omit addrtype on kernels where xt_addrtype is absent (common
     * on Kernel 4.14 and below). The rule is broader but still functional.
     *
     * We record which variant was actually inserted in the state file so
     * ds_ipt_remove_portforwards can issue the exact matching -D later. */
    int dnat_ok = 0;
    const char *inserted_variant = NULL;

    if (use_addrtype) {
      char *dnat[] = {"iptables",
                      "-t",
                      "nat",
                      "-I",
                      "PREROUTING",
                      "1",
                      "-p",
                      pf->proto,
                      "-m",
                      "addrtype",
                      "--dst-type",
                      "LOCAL",
                      "--dport",
                      host_port_str,
                      "-j",
                      "DNAT",
                      "--to-destination",
                      to_dest,
                      NULL};
      dnat_ok = (run_command_log(dnat) == 0);
      if (dnat_ok)
        inserted_variant = "addrtype";
      else
        ds_warn("portforward: DNAT+addrtype failed for port %s, "
                "retrying without addrtype",
                host_port_str);
    }

    if (!dnat_ok) {
      /* Fallback: no addrtype match - broader rule, still correct for
       * single-interface phones. Log a notice so the user is aware. */
      if (!use_addrtype)
        ds_log("[IPT] xt_addrtype unavailable - using basic DNAT for port %s",
               host_port_str);
      char *dnat_fb[] = {"iptables",         "-t",          "nat", "-I",
                         "PREROUTING",       "1",           "-p",  pf->proto,
                         "--dport",          host_port_str, "-j",  "DNAT",
                         "--to-destination", to_dest,       NULL};
      if (run_command_log(dnat_fb) == 0)
        inserted_variant = "basic";
      else
        ds_warn("portforward: DNAT insert failed for port %s", host_port_str);
    }

    /* FORWARD ACCEPT */
    char *fwd[] = {
        "iptables", "-I",          "FORWARD", "1",
        "-p",       pf->proto,     "-d",      (char *)(uintptr_t)container_ip,
        "--dport",  cont_port_str, "-j",      "ACCEPT",
        NULL};
    if (run_command_quiet(fwd) != 0)
      ds_warn("portforward: FORWARD insert failed for port %s", cont_port_str);

    /* Record this rule in the state file only if the DNAT insert succeeded.
     * The FORWARD rule is always attempted; if it failed ds_warn was already
     * emitted, but we still record the entry so removal can clean up the
     * DNAT side on stop. */
    if (inserted_variant)
      pf_state_append(state_f, inserted_variant, pf->proto, host_port_str,
                      to_dest, cont_port_str);

    /* Localhost forwarding: 127.0.0.1:<host_port> -> <container_ip>:<cont_port>
     * Same proto/single-port/range coverage as the LAN-facing rules above -
     * host_port_str/cont_port_str/to_dest are already formatted for ranges
     * (START:END / START-END) by pf_fmt_ports, so this works identically for
     * tcp, udp, single ports, and symmetric/asymmetric-width ranges.
     *
     * OUTPUT DNAT rewrites packets a local process sends to lo:host_port so
     * they target the container. POSTROUTING MASQUERADE rewrites the source
     * back to the container's gateway IP so return traffic routes correctly
     * (without it the container sees the real 127.0.0.1 source and replies
     * are dropped/misrouted). */
    char *lo_dnat[] = {
        "iptables", "-t",          "nat",     "-I",   "OUTPUT",
        "1",        "-p",          pf->proto, "-o",   "lo",
        "--dport",  host_port_str, "-j",      "DNAT", "--to-destination",
        to_dest,    NULL};
    int lo_ok = (run_command_log(lo_dnat) == 0);
    if (!lo_ok)
      ds_warn("portforward: localhost OUTPUT DNAT failed for port %s",
              host_port_str);

    char *lo_masq[] = {"iptables",    "-t",
                       "nat",         "-I",
                       "POSTROUTING", "1",
                       "-p",          pf->proto,
                       "-d",          (char *)(uintptr_t)container_ip,
                       "--dport",     cont_port_str,
                       "-j",          "MASQUERADE",
                       NULL};
    int masq_ok = (run_command_quiet(lo_masq) == 0);
    if (!masq_ok)
      ds_warn("portforward: localhost POSTROUTING MASQUERADE failed for "
              "port %s",
              cont_port_str);

    if (lo_ok)
      pf_state_append(state_f, "local_out", pf->proto, host_port_str, to_dest,
                      cont_port_str);
    if (masq_ok)
      pf_state_append(state_f, "local_post", pf->proto, host_port_str, to_dest,
                      cont_port_str);
  }

  if (state_f)
    fclose(state_f);

  return 0;
}

/* Public API: ds_ipt_remove_portforwards
 *
 * Three-pass cleanup strategy, in order of reliability:
 *
 *   Pass 1 - state file (primary, always preferred)
 *     Reads the state file written by ds_ipt_add_portforwards and issues
 *     iptables -D using the exact args that were used to insert each rule.
 *     This is immune to config-drift: it works correctly even if the user
 *     added or removed port-forward entries in the config while the container
 *     was running.
 *
 *   Pass 2 - cfg->port_forwards loop (safety net)
 *     Iterates the current config and attempts deletion of both the addrtype
 *     and basic DNAT variants. This catches rules added before the state file
 *     feature existed (upgrades from older versions). run_command_quiet
 *     silently ignores rules that no longer exist.
 *
 *   Pass 3 - iptables-save shell sweep (last resort)
 *     Only runs when no state file was found. Scans live iptables rules for
 *     anything targeting this container IP and removes them. Catches orphaned
 *     rules from container crashes or pre-state-file installations. */

int ds_ipt_remove_portforwards(struct ds_config *cfg) {
  if (!cfg)
    return 0;

  /* Resolve the container IP: prefer the runtime-assigned address; fall back
   * to the configured static IP so cleanup works even if the container never
   * fully started (e.g., crashed during boot before nat_container_ip was set).
   */
  const char *container_ip =
      cfg->nat_container_ip[0] ? cfg->nat_container_ip : cfg->static_nat_ip;
  if (!container_ip || container_ip[0] == '\0')
    return 0;

  /* Pass 1: state file */
  int had_state = pf_state_remove(container_ip);

  /* Pass 2: cfg->port_forwards safety net */
  /* Attempt both addrtype and basic DNAT variants for every rule currently in
   * the config. Whichever variant wasn't actually inserted will return a
   * non-zero exit code from iptables; run_command_quiet ignores it. */
  for (int i = 0; i < cfg->port_forward_count; i++) {
    struct ds_port_forward *pf = &cfg->port_forwards[i];

    char host_port_str[16], cont_port_str[16], to_dest[80];
    pf_fmt_ports(pf, container_ip, host_port_str, cont_port_str, to_dest);

    /* addrtype variant */
    char *del_at[] = {
        "iptables",    "-t",         "nat",     "-D",
        "PREROUTING",  "-p",         pf->proto, "-m",
        "addrtype",    "--dst-type", "LOCAL",   "--dport",
        host_port_str, "-j",         "DNAT",    "--to-destination",
        to_dest,       NULL};
    run_command_quiet(del_at);

    /* basic variant (no addrtype) */
    char *del_basic[] = {"iptables",    "-t", "nat",     "-D",
                         "PREROUTING",  "-p", pf->proto, "--dport",
                         host_port_str, "-j", "DNAT",    "--to-destination",
                         to_dest,       NULL};
    run_command_quiet(del_basic);

    /* FORWARD ACCEPT */
    char *del_fwd[] = {"iptables",
                       "-D",
                       "FORWARD",
                       "-p",
                       pf->proto,
                       "-d",
                       (char *)(uintptr_t)container_ip,
                       "--dport",
                       cont_port_str,
                       "-j",
                       "ACCEPT",
                       NULL};
    run_command_quiet(del_fwd);

    /* Localhost OUTPUT DNAT */
    char *del_lo[] = {"iptables",    "-t",
                      "nat",         "-D",
                      "OUTPUT",      "-p",
                      pf->proto,     "-o",
                      "lo",          "--dport",
                      host_port_str, "-j",
                      "DNAT",        "--to-destination",
                      to_dest,       NULL};
    run_command_quiet(del_lo);

    /* Localhost POSTROUTING MASQUERADE */
    char *del_masq[] = {
        "iptables",   "-t",          "nat",
        "-D",         "POSTROUTING", "-p",
        pf->proto,    "-d",          (char *)(uintptr_t)container_ip,
        "--dport",    cont_port_str, "-j",
        "MASQUERADE", NULL};
    run_command_quiet(del_masq);
  }

  /* Pass 3: iptables-save shell sweep (fallback)
   */
  /* Only runs when no state file existed - i.e., the container was started by
   * an older version of Droidspaces that did not write state files, or the
   * process crashed before the file could be written.
   * We intentionally avoid this path in the normal case: parsing iptables-save
   * output through a shell is slower and depends on the host having sh(1). */
  /* Defense-in-depth: container_ip is already validated to 172.28.x.x at every
   * ingest point, but Pass 3 is the only path that interpolates it into a shell
   * command.  Re-validate it as a plain IPv4 literal and skip the sweep
   * otherwise, so no shell metacharacter can ever reach sh -c. */
  struct in_addr ip_check;
  int ip_ok = (inet_pton(AF_INET, container_ip, &ip_check) == 1);
  if (!had_state && !ip_ok)
    ds_warn("Skipping iptables-save shell sweep: container IP '%s' is not a "
            "valid IPv4 literal",
            container_ip);

  if (!had_state && ip_ok) {
    char cmd[512];

    /* Remove PREROUTING DNAT rules whose --to-destination targets this IP */
    snprintf(cmd, sizeof(cmd),
             "iptables-save -t nat | grep ' -A PREROUTING ' | "
             "grep -- '--to-destination %s:' | "
             "sed 's/ -A / -D /' | "
             "while IFS= read -r rule; do iptables -t nat $rule; done",
             container_ip);
    char *sh_nat[] = {"sh", "-c", cmd, NULL};
    run_command_quiet(sh_nat);

    /* Remove FORWARD ACCEPT rules whose -d targets this IP */
    snprintf(cmd, sizeof(cmd),
             "iptables-save -t filter | grep ' -A FORWARD ' | "
             "grep -- ' -d %s ' | grep ' -j ACCEPT' | "
             "sed 's/ -A / -D /' | "
             "while IFS= read -r rule; do iptables -t filter $rule; done",
             container_ip);
    char *sh_fwd[] = {"sh", "-c", cmd, NULL};
    run_command_quiet(sh_fwd);

    /* Remove localhost OUTPUT DNAT rules whose --to-destination targets
     * this IP (127.0.0.1:HOST -> container_ip:PORT). */
    snprintf(cmd, sizeof(cmd),
             "iptables-save -t nat | grep ' -A OUTPUT ' | "
             "grep -- '-o lo ' | grep -- '--to-destination %s:' | "
             "sed 's/ -A / -D /' | "
             "while IFS= read -r rule; do iptables -t nat $rule; done",
             container_ip);
    char *sh_lo[] = {"sh", "-c", cmd, NULL};
    run_command_quiet(sh_lo);

    /* Remove localhost POSTROUTING MASQUERADE rules whose -d + --dport
     * target this IP. Filtered on --dport too so the general uplink
     * MASQUERADE rule (whole subnet, no -d/--dport) is never touched. */
    snprintf(cmd, sizeof(cmd),
             "iptables-save -t nat | grep ' -A POSTROUTING ' | "
             "grep -- ' -d %s/32 ' | grep -- '--dport' | "
             "grep ' -j MASQUERADE' | sed 's/ -A / -D /' | "
             "while IFS= read -r rule; do iptables -t nat $rule; done",
             container_ip);
    char *sh_masq[] = {"sh", "-c", cmd, NULL};
    run_command_quiet(sh_masq);
  }

  return 0;
}
