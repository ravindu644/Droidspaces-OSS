/*
 * Droidspaces v5 — High-performance Container Runtime
 *
 * Surgical iptables rule management via raw IP_TABLES socket API.
 * Replaces all `iptables` shell invocations.
 *
 * ── Android Safety Contract (NEVER violate these) ──────────────────────────
 *   • Never flush any chain (would kill Android tethering/hotspot)
 *   • Never change any chain policy
 *   • Never touch rules we did not create
 *   • Only INSERT rules scoped to DS_NAT_BRIDGE / DS_DEFAULT_SUBNET
 *   • Always check existence before inserting (fully idempotent)
 *
 * ── Kernel / API compatibility ──────────────────────────────────────────────
 *   • Kernel 3.18+ (Android/Linux)
 *   • Uses getsockopt/setsockopt on AF_INET SOCK_RAW with IPPROTO_RAW
 *   • Falls back to iptables(8) binary on ENOPROTOOPT / EOPNOTSUPP / any
 *     kernel rejection
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

/* ---------------------------------------------------------------------------
 * CIDR helper — shared with network.c via the public header
 * ---------------------------------------------------------------------------*/

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

/* ---------------------------------------------------------------------------
 * Module loader — best-effort, harmless on built-in or absent modprobe
 * ---------------------------------------------------------------------------*/

static int modules_probed = 0;

static void probe_iptables_modules(void) {
  if (modules_probed)
    return;
  modules_probed = 1;

  char *mods[] = {"iptable_nat",
                  "iptable_filter",
                  "iptable_mangle",
                  "ip_conntrack",
                  "xt_conntrack",
                  "nf_nat",
                  NULL};
  for (int i = 0; mods[i]; i++) {
    char *a[] = {"modprobe", "-q", mods[i], NULL};
    run_command_quiet(a);
  }
}

/* ---------------------------------------------------------------------------
 * Internal: get table info + entries blob via getsockopt
 *
 * Returns 0 on success.  *entries_out points to the allocated
 * ipt_get_entries struct; caller frees it.
 *
 * Convenience macro: ENTRIES_BLOB(base) → pointer to the raw rule bytes
 * inside the ipt_get_entries allocation.
 * ---------------------------------------------------------------------------*/

static int get_table(int fd, const char *table_name, struct ipt_getinfo *info,
                     unsigned char **entries_out) {
  memset(info, 0, sizeof(*info));
  safe_strncpy(info->name, table_name, sizeof(info->name));
  socklen_t info_len = sizeof(*info);

  if (getsockopt(fd, IPPROTO_IP, IPT_SO_GET_INFO, info, &info_len) < 0) {
    ds_log("[IPT] get_table('%s') GET_INFO failed: %s", table_name,
           strerror(errno));
    return -errno;
  }

  size_t esz = sizeof(struct ipt_get_entries) + info->size;
  struct ipt_get_entries *entries = calloc(1, esz);
  if (!entries)
    return -ENOMEM;

  safe_strncpy(entries->name, table_name, sizeof(entries->name));
  entries->size = info->size;
  socklen_t elen = (socklen_t)esz;

  if (getsockopt(fd, IPPROTO_IP, IPT_SO_GET_ENTRIES, entries, &elen) < 0) {
    int err = errno;
    ds_log("[IPT] get_table('%s') GET_ENTRIES failed: %s", table_name,
           strerror(err));
    free(entries);
    return -err;
  }

  *entries_out = (unsigned char *)entries;
  return 0;
}

#define ENTRIES_BLOB(base)                                                     \
  ((unsigned char *)((struct ipt_get_entries *)(base))->entrytable)

/* ---------------------------------------------------------------------------
 * Internal: walk the blob to find an existing rule matching our fingerprint.
 *
 * All non-NULL/non-zero criteria must match simultaneously.
 * ---------------------------------------------------------------------------*/

static int rule_exists_in_hook(const struct ipt_getinfo *info,
                               const unsigned char *blob, unsigned int hook_id,
                               const char *iface_in, const char *iface_out,
                               uint32_t src, uint32_t src_mask,
                               const char *target_name) {
  if (!(info->valid_hooks & (1u << hook_id)))
    return 0;

  unsigned int off = info->hook_entry[hook_id];
  unsigned int end = info->underflow[hook_id];

  while (off < end) {
    const struct ipt_entry *e = (const struct ipt_entry *)(blob + off);
    if (e->next_offset == 0 || e->next_offset > end - off)
      break;

    const struct xt_entry_target *t =
        (const struct xt_entry_target *)((const uint8_t *)e + e->target_offset);

    int match = 1;

    if (target_name && target_name[0] &&
        strcmp(t->u.user.name, target_name) != 0)
      match = 0;
    if (match && iface_in && iface_in[0] &&
        strncmp(e->ip.iniface, iface_in, IFNAMSIZ) != 0)
      match = 0;
    if (match && iface_out && iface_out[0] &&
        strncmp(e->ip.outiface, iface_out, IFNAMSIZ) != 0)
      match = 0;
    if (match && src != 0 &&
        (e->ip.src.s_addr != src || e->ip.smsk.s_addr != src_mask))
      match = 0;

    if (match)
      return 1;
    off += e->next_offset;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Internal: fixup_jump_targets
 *
 * After inserting new_rule_sz bytes at insert_off, any xt_standard_target
 * with a positive verdict (= absolute byte offset = chain jump) that pointed
 * to an entry AT OR AFTER insert_off must be incremented by new_rule_sz.
 * ---------------------------------------------------------------------------*/

static void fixup_jump_targets(unsigned char *blob, unsigned int blob_sz,
                               unsigned int insert_off, unsigned int delta) {
  unsigned int off = 0;

  while (off < blob_sz) {
    struct ipt_entry *e = (struct ipt_entry *)(blob + off);

    if (e->next_offset < sizeof(*e) || off + e->next_offset > blob_sz)
      break;

    if (e->target_offset + sizeof(struct xt_standard_target) <=
        e->next_offset) {
      struct xt_entry_target *t =
          (struct xt_entry_target *)((uint8_t *)e + e->target_offset);

      if (t->u.user.name[0] == '\0' &&
          t->u.target_size ==
              (uint16_t)XT_ALIGN(sizeof(struct xt_standard_target))) {
        struct xt_standard_target *st = (struct xt_standard_target *)t;
        if (st->verdict >= (int)insert_off)
          st->verdict += (int)delta;
      }
    }

    off += e->next_offset;
  }
}

/* ---------------------------------------------------------------------------
 * Internal: insert_rule_at_hook
 *
 * Inserts new_rule at the very beginning of the given hook's chain.
 *
 * ── CRITICAL BUG FIX ────────────────────────────────────────────────────────
 * The hook_entry/underflow offset adjustment was previously:
 *
 *     if (info->hook_entry[h] >= insert_off)   ← BUG
 *         repl->hook_entry[h] += new_rule_size;
 *
 * Because insert_off == info->hook_entry[hook_id], the hook we are inserting
 * INTO was always bumped past the newly inserted rule — making it unreachable
 * to the kernel's packet-processing path.  This caused ZERO packet matches
 * on every FORWARD/POSTROUTING rule we ever inserted.
 *
 * The correct semantics are:
 *   hook_entry: bump only if STRICTLY GREATER than insert_off (>) because
 *               the chain we insert into should still start at insert_off
 *               (where our new rule now lives).
 *   underflow:  bump if >= insert_off (the terminal/policy entry at the end
 *               of any chain at-or-after the insertion point always shifts).
 * ────────────────────────────────────────────────────────────────────────────
 * ---------------------------------------------------------------------------*/

static int insert_rule_at_hook(int fd, const char *table_name,
                               struct ipt_getinfo *info,
                               unsigned char *old_blob, unsigned int hook_id,
                               const void *new_rule, unsigned int new_rule_sz) {
  unsigned int insert_off = info->hook_entry[hook_id];
  unsigned int new_sz = info->size + new_rule_sz;

  ds_log("[IPT] insert_rule_at_hook: table='%s' hook=%u insert_off=%u "
         "old_sz=%u new_sz=%u",
         table_name, hook_id, insert_off, info->size, new_sz);

  size_t replace_sz = sizeof(struct ipt_replace) + new_sz;
  struct ipt_replace *repl = calloc(1, replace_sz);
  if (!repl)
    return -ENOMEM;

  safe_strncpy(repl->name, table_name, sizeof(repl->name));
  repl->valid_hooks = info->valid_hooks;
  repl->num_entries = info->num_entries + 1;
  repl->size = new_sz;

  /* ── Offset fix (see comment above) ── */
  for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
    if (!(info->valid_hooks & (1u << h)))
      continue;

    repl->hook_entry[h] = info->hook_entry[h];
    repl->underflow[h] = info->underflow[h];

    /* hook_entry: strictly greater — the chain we insert INTO keeps its start
     */
    if (info->hook_entry[h] > insert_off)
      repl->hook_entry[h] += new_rule_sz;

    /* underflow: at-or-after — terminal entries always shift */
    if (info->underflow[h] >= insert_off)
      repl->underflow[h] += new_rule_sz;

    ds_log("[IPT]   hook[%d]: entry %u→%u  underflow %u→%u", h,
           info->hook_entry[h], repl->hook_entry[h], info->underflow[h],
           repl->underflow[h]);
  }

  repl->num_counters = repl->num_entries;
  repl->counters = calloc(repl->num_entries, sizeof(struct xt_counters));
  if (!repl->counters && repl->num_entries > 0) {
    free(repl);
    return -ENOMEM;
  }
  repl->size = new_sz;

  /* Build new blob: prefix | new_rule | suffix */
  unsigned char *nb = (unsigned char *)(repl + 1);
  if (insert_off > 0)
    memcpy(nb, old_blob, insert_off);
  memcpy(nb + insert_off, new_rule, new_rule_sz);
  if (info->size > insert_off)
    memcpy(nb + insert_off + new_rule_sz, old_blob + insert_off,
           info->size - insert_off);

  /* BUG-1 FIX: patch stale jump verdicts in the shifted suffix */
  fixup_jump_targets(nb, new_sz, insert_off, new_rule_sz);

  int attempts = 3;
  int ret = 0;
  int err = 0;

  while (attempts-- > 0) {
    ret = setsockopt(fd, IPPROTO_IP, IPT_SO_SET_REPLACE, repl,
                     (socklen_t)replace_sz);
    err = errno;

    if (ret == 0)
      break;

    if (err == EAGAIN && attempts > 0) {
      usleep(20000 * (3 - attempts));
      /* We loop here; technically caller could re-fetch info/old_blob,
         but a simple sleep is often enough to pass the table lock */
      continue;
    }

    break;
  }

  free(repl->counters);
  free(repl);

  if (ret < 0) {
    if (err != ENOENT && err != EAGAIN)
      ds_log("[IPT] insert_rule_at_hook: kernel rejected blob (errno=%d %s)",
             err, strerror(err));
    return -err;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Internal: remove_matching_rules
 *
 * Builds a new blob that omits every entry matching any of our fingerprints,
 * then submits it atomically via IPT_SO_SET_REPLACE.
 *
 * Two-pass algorithm avoids in-place mutation of the blob being walked.
 * ---------------------------------------------------------------------------*/

static int remove_matching_rules(int fd, const char *table_name,
                                 struct ipt_getinfo *info,
                                 unsigned char *old_blob, uint32_t match_src,
                                 uint32_t match_mask, const char *match_iface) {
  unsigned char *new_blob = malloc(info->size);
  if (!new_blob)
    return -ENOMEM;

  /* Allocate per-entry tracking arrays */
  unsigned int *old_offsets =
      calloc(info->num_entries + 1, sizeof(unsigned int));
  unsigned int *removed_before =
      calloc(info->num_entries + 1, sizeof(unsigned int));
  if (!old_offsets || !removed_before) {
    free(new_blob);
    free(old_offsets);
    free(removed_before);
    return -ENOMEM;
  }

  unsigned int new_sz = 0;
  unsigned int removed_count = 0;
  unsigned int cumulative_gone = 0;
  unsigned int offset = 0;
  unsigned int ei = 0;

  /* Pass 1: walk, classify, build new_blob */
  while (offset < info->size && ei < info->num_entries) {
    const struct ipt_entry *e = (const struct ipt_entry *)(old_blob + offset);
    if (e->next_offset == 0)
      break;

    old_offsets[ei] = offset;
    removed_before[ei] = cumulative_gone;

    const struct xt_entry_target *t =
        (const struct xt_entry_target *)((const uint8_t *)e + e->target_offset);
    const char *tname = t->u.user.name;

    int is_ours = 0;

    /* MASQUERADE for our subnet */
    if (match_src && strcmp(tname, "MASQUERADE") == 0 &&
        e->ip.src.s_addr == match_src && e->ip.smsk.s_addr == match_mask)
      is_ours = 1;

    /* ACCEPT on our bridge interface */
    if (!is_ours && match_iface && match_iface[0] &&
        strcmp(tname, "ACCEPT") == 0) {
      if (strncmp(e->ip.iniface, match_iface, IFNAMSIZ) == 0 ||
          strncmp(e->ip.outiface, match_iface, IFNAMSIZ) == 0)
        is_ours = 1;
    }
    /* ACCEPT on our bridge interface — raw-path variant (empty name, standard
     * target) */
    if (!is_ours && match_iface && match_iface[0] && tname[0] == '\0') {
      if (strncmp(e->ip.iniface, match_iface, IFNAMSIZ) == 0 ||
          strncmp(e->ip.outiface, match_iface, IFNAMSIZ) == 0)
        is_ours = 1;
    }

    if (is_ours) {
      /* Safety: never remove an underflow (chain policy) entry.
       * This check belongs here in Pass 1, not Pass 2 — we only care
       * whether the entry we are ABOUT TO REMOVE happens to be an underflow.
       * Checking old_offsets[] in Pass 2 matches every entry unconditionally
       * and always fires, which was the false-positive bug.               */
      int is_underflow = 0;
      for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
        if ((info->valid_hooks & (1u << h)) && offset == info->underflow[h]) {
          is_underflow = 1;
          break;
        }
      }
      if (is_underflow) {
        ds_warn("[IPT] remove: would remove underflow entry at offset %u — "
                "skipping",
                offset);
        /* Treat as non-matching — copy it through instead of removing */
        memcpy(new_blob + new_sz, e, e->next_offset);
        new_sz += e->next_offset;
        offset += e->next_offset;
        ei++;
        continue;
      }
      ds_log("[IPT] remove: dropping '%s' rule at offset %u", tname, offset);
      cumulative_gone += e->next_offset;
      removed_count++;
    } else {
      memcpy(new_blob + new_sz, e, e->next_offset);
      new_sz += e->next_offset;
    }

    offset += e->next_offset;
    ei++;
  }
  old_offsets[ei] = offset;
  removed_before[ei] = cumulative_gone;

  if (removed_count == 0) {
    free(new_blob);
    free(old_offsets);
    free(removed_before);
    return 0; /* nothing to do */
  }

  /* Build ipt_replace */
  size_t replace_sz = sizeof(struct ipt_replace) + new_sz;
  struct ipt_replace *repl = calloc(1, replace_sz);
  if (!repl) {
    free(new_blob);
    free(old_offsets);
    free(removed_before);
    return -ENOMEM;
  }

  safe_strncpy(repl->name, table_name, sizeof(repl->name));
  repl->valid_hooks = info->valid_hooks;
  repl->num_entries = info->num_entries - removed_count;
  repl->size = new_sz;
  repl->num_counters = repl->num_entries;
  repl->counters = calloc(repl->num_entries, sizeof(struct xt_counters));
  if (!repl->counters && repl->num_entries > 0) {
    free(repl);
    free(new_blob);
    free(old_offsets);
    free(removed_before);
    return -ENOMEM;
  }

  /* Pass 2: fix up hook_entry / underflow offsets */
  for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
    if (!(info->valid_hooks & (1u << h)))
      continue;

    /* Find cumulative bytes removed before hook_entry[h] */
    unsigned int adj = 0;
    for (unsigned int k = 0; k <= ei; k++) {
      if (old_offsets[k] >= info->hook_entry[h]) {
        adj = removed_before[k];
        break;
      }
    }
    repl->hook_entry[h] = info->hook_entry[h] - adj;

    adj = 0;
    for (unsigned int k = 0; k <= ei; k++) {
      if (old_offsets[k] >= info->underflow[h]) {
        adj = removed_before[k];
        break;
      }
    }
    repl->underflow[h] = info->underflow[h] - adj;
  }

  memcpy(repl + 1, new_blob, new_sz);

  int attempts = 3;
  int ret = 0;
  int err = 0;

  while (attempts-- > 0) {
    ret = setsockopt(fd, IPPROTO_IP, IPT_SO_SET_REPLACE, repl,
                     (socklen_t)replace_sz);
    err = errno;

    if (ret == 0)
      break;

    if (err == EAGAIN && attempts > 0) {
      usleep(20000 * (3 - attempts));
      continue;
    }

    break;
  }

  free(repl->counters);
  free(repl);
  free(new_blob);
  free(old_offsets);
  free(removed_before);

  return (ret < 0) ? -err : 0;
}

/* ---------------------------------------------------------------------------
 * Internal: open a raw socket (shared across public APIs)
 * ---------------------------------------------------------------------------*/

static int should_use_raw_api(void) {
  if (is_android())
    return 1;

  FILE *f = fopen("/proc/net/ip_tables_names", "r");
  if (!f)
    return 0;

  char line[64];
  int found_filter = 0, found_nat = 0;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "filter", 6) == 0)
      found_filter = 1;
    if (strncmp(line, "nat", 3) == 0)
      found_nat = 1;
  }
  fclose(f);

  return (found_filter && found_nat) ? 1 : 0;
}

static int open_raw_socket(void) {
  probe_iptables_modules();
  int fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
  if (fd < 0)
    ds_log("[IPT] Failed to open raw socket: %s", strerror(errno));
  return fd;
}

/* ---------------------------------------------------------------------------
 * Public API: ds_ipt_ensure_masquerade
 *
 * Inserts: -t nat -I POSTROUTING 1 -s <cidr> ! -d <cidr> -j MASQUERADE
 * ---------------------------------------------------------------------------*/

int ds_ipt_ensure_masquerade(const char *src_cidr) {
  ds_log("[IPT] ensure_masquerade: cidr=%s", src_cidr);

  uint32_t src_ip, src_mask;
  parse_cidr(src_cidr, &src_ip, &src_mask);

  if (!should_use_raw_api())
    goto binary_fallback_masq;

  int fd = open_raw_socket();
  if (fd < 0) {
    /* No socket at all — binary only */
    goto binary_fallback_masq;
  }

  {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    int ret = get_table(fd, "nat", &info, &base);
    if (ret < 0) {
      close(fd);
      if (ret == -ENOENT || ret == -ENOPROTOOPT || ret == -EACCES)
        goto binary_fallback_masq;
      return ret;
    }

    /* Idempotency check */
    if (rule_exists_in_hook(&info, ENTRIES_BLOB(base), NF_INET_POST_ROUTING,
                            NULL, NULL, src_ip, src_mask, "MASQUERADE")) {
      ds_log("[IPT] MASQUERADE already present — skipping");
      free(base);
      close(fd);
      return 0;
    }

    /* Build MASQUERADE rule.
     * MASQUERADE target requires the nf_nat_ipv4_multi_range_compat payload
     * with rangesize=1 — the kernel validates this. */
    unsigned char
        rule_buf[XT_ALIGN(sizeof(struct ipt_entry)) +
                 XT_ALIGN(sizeof(struct xt_entry_target) +
                          sizeof(struct nf_nat_ipv4_multi_range_compat))];
    memset(rule_buf, 0, sizeof(rule_buf));

    struct ipt_entry *re = (struct ipt_entry *)rule_buf;
    struct xt_entry_target *rt =
        (struct xt_entry_target *)(rule_buf +
                                   XT_ALIGN(sizeof(struct ipt_entry)));
    struct nf_nat_ipv4_multi_range_compat *mr =
        (struct nf_nat_ipv4_multi_range_compat *)(rt->data);

    re->ip.src.s_addr = src_ip;
    re->ip.smsk.s_addr = src_mask;
    re->ip.dst.s_addr = src_ip;
    re->ip.dmsk.s_addr = src_mask;
    re->ip.invflags = IPT_INV_DSTIP; /* ! -d */
    re->target_offset = (__u16)XT_ALIGN(sizeof(struct ipt_entry));
    re->next_offset = (__u16)sizeof(rule_buf);

    rt->u.target_size =
        (__u16)XT_ALIGN(sizeof(struct xt_entry_target) +
                        sizeof(struct nf_nat_ipv4_multi_range_compat));
    safe_strncpy(rt->u.user.name, "MASQUERADE", sizeof(rt->u.user.name));
    mr->rangesize = 1;

    ret = insert_rule_at_hook(fd, "nat", &info, ENTRIES_BLOB(base),
                              NF_INET_POST_ROUTING, rule_buf, sizeof(rule_buf));
    free(base);
    close(fd);

    if (ret == 0) {
      ds_log("[IPT] MASQUERADE inserted via raw socket API");
      return 0;
    }
    ds_log("[IPT] Raw insert failed (ret=%d), falling back to binary", ret);
  }

binary_fallback_masq:
  ds_log("[IPT] MASQUERADE binary fallback");
  char *argv[] = {"iptables",
                  "-t",
                  "nat",
                  "-I",
                  "POSTROUTING",
                  "1",
                  "-s",
                  (char *)(uintptr_t)src_cidr,
                  "!",
                  "-d",
                  (char *)(uintptr_t)src_cidr,
                  "-j",
                  "MASQUERADE",
                  NULL};
  return run_command_quiet(argv);
}

/* ---------------------------------------------------------------------------
 * Public API: ds_ipt_ensure_forward_accept
 *
 * Inserts:
 *   -t filter -I FORWARD 1 -i <iface> -j ACCEPT
 *   -t filter -I FORWARD 1 -o <iface> -j ACCEPT
 *
 * Called with DS_NAT_BRIDGE ("ds-br0") when bridge-nf-call-iptables=1 so
 * the FORWARD hook sees the bridge interface name as the ingress/egress.
 * ---------------------------------------------------------------------------*/

int ds_ipt_ensure_forward_accept(const char *iface) {
  ds_log("[IPT] ensure_forward_accept: iface=%s", iface);

  if (!should_use_raw_api())
    goto binary_fallback_fwd;

  int fd = open_raw_socket();
  if (fd < 0)
    goto binary_fallback_fwd;

  /* ── -i iface ── */
  {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    int ret = get_table(fd, "filter", &info, &base);
    if (ret < 0) {
      close(fd);
      if (ret == -ENOENT || ret == -ENOPROTOOPT || ret == -EACCES ||
          ret == -EOPNOTSUPP)
        goto binary_fallback_fwd;
      return ret;
    }

    if (!rule_exists_in_hook(&info, ENTRIES_BLOB(base), NF_INET_FORWARD, iface,
                             NULL, 0, 0, NULL)) {
      unsigned char rule_buf[XT_ALIGN(sizeof(struct ipt_entry)) +
                             XT_ALIGN(sizeof(struct xt_standard_target))];
      memset(rule_buf, 0, sizeof(rule_buf));

      struct ipt_entry *re = (struct ipt_entry *)rule_buf;
      struct xt_standard_target *st =
          (struct xt_standard_target *)(rule_buf +
                                        XT_ALIGN(sizeof(struct ipt_entry)));

      safe_strncpy(re->ip.iniface, iface, IFNAMSIZ);
      memset(re->ip.iniface_mask, 0xff, strlen(iface) + 1);
      re->target_offset = (__u16)XT_ALIGN(sizeof(struct ipt_entry));
      re->next_offset = (__u16)sizeof(rule_buf);

      st->target.u.target_size =
          (__u16)XT_ALIGN(sizeof(struct xt_standard_target));
      st->target.u.user.name[0] = '\0';
      st->target.u.user.revision = 0;
      st->verdict = -NF_ACCEPT - 1;

      ret = insert_rule_at_hook(fd, "filter", &info, ENTRIES_BLOB(base),
                                NF_INET_FORWARD, rule_buf, sizeof(rule_buf));
      if (ret == 0) {
        ds_log("[IPT] FORWARD -i %s ACCEPT inserted", iface);
      } else {
        ds_log("[IPT] Raw insert -i %s failed (ret=%d), using binary", iface,
               ret);
        char *a[] = {"iptables", "-I",     "FORWARD",
                     "1",        "-i",     (char *)(uintptr_t)iface,
                     "-j",       "ACCEPT", NULL};
        run_command_quiet(a);
      }
    } else {
      ds_log("[IPT] FORWARD -i %s ACCEPT already present", iface);
    }
    free(base);
  }

  /* Re-read table for the second insert */
  {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    int ret = get_table(fd, "filter", &info, &base);
    if (ret < 0) {
      close(fd);
      if (ret == -ENOENT || ret == -ENOPROTOOPT || ret == -EACCES ||
          ret == -EOPNOTSUPP)
        goto binary_fallback_fwd;
      return ret;
    }

    if (!rule_exists_in_hook(&info, ENTRIES_BLOB(base), NF_INET_FORWARD, NULL,
                             iface, 0, 0, NULL)) {
      unsigned char rule_buf[XT_ALIGN(sizeof(struct ipt_entry)) +
                             XT_ALIGN(sizeof(struct xt_standard_target))];
      memset(rule_buf, 0, sizeof(rule_buf));

      struct ipt_entry *re = (struct ipt_entry *)rule_buf;
      struct xt_standard_target *st =
          (struct xt_standard_target *)(rule_buf +
                                        XT_ALIGN(sizeof(struct ipt_entry)));

      safe_strncpy(re->ip.outiface, iface, IFNAMSIZ);
      memset(re->ip.outiface_mask, 0xff, strlen(iface) + 1);
      re->target_offset = (__u16)XT_ALIGN(sizeof(struct ipt_entry));
      re->next_offset = (__u16)sizeof(rule_buf);

      st->target.u.target_size =
          (__u16)XT_ALIGN(sizeof(struct xt_standard_target));
      st->target.u.user.name[0] = '\0';
      st->target.u.user.revision = 0;
      st->verdict = -NF_ACCEPT - 1;

      ret = insert_rule_at_hook(fd, "filter", &info, ENTRIES_BLOB(base),
                                NF_INET_FORWARD, rule_buf, sizeof(rule_buf));
      if (ret == 0) {
        ds_log("[IPT] FORWARD -o %s ACCEPT inserted", iface);
      } else {
        ds_log("[IPT] Raw insert -o %s failed (ret=%d), using binary", iface,
               ret);
        char *a[] = {"iptables", "-I",     "FORWARD",
                     "1",        "-o",     (char *)(uintptr_t)iface,
                     "-j",       "ACCEPT", NULL};
        run_command_quiet(a);
      }
    } else {
      ds_log("[IPT] FORWARD -o %s ACCEPT already present", iface);
    }
    free(base);
  }

  close(fd);
  return 0;

binary_fallback_fwd:
  ds_log("[IPT] FORWARD ACCEPT binary fallback for iface=%s", iface);
  {
    char *a[] = {"iptables", "-I",     "FORWARD",
                 "1",        "-i",     (char *)(uintptr_t)iface,
                 "-j",       "ACCEPT", NULL};
    run_command_quiet(a);
  }
  {
    char *a[] = {"iptables", "-I",     "FORWARD",
                 "1",        "-o",     (char *)(uintptr_t)iface,
                 "-j",       "ACCEPT", NULL};
    return run_command_quiet(a);
  }
}

/* ---------------------------------------------------------------------------
 * Public API: ds_ipt_ensure_input_accept
 *
 * Inserts: -t filter -I INPUT 1 -i <iface> -j ACCEPT
 * ---------------------------------------------------------------------------*/

int ds_ipt_ensure_input_accept(const char *iface) {
  ds_log("[IPT] ensure_input_accept: iface=%s", iface);

  if (!should_use_raw_api())
    goto binary_fallback_inp;

  int fd = open_raw_socket();
  if (fd < 0)
    goto binary_fallback_inp;

  struct ipt_getinfo info;
  unsigned char *base = NULL;
  int ret = get_table(fd, "filter", &info, &base);
  if (ret < 0) {
    close(fd);
    if (ret == -ENOENT || ret == -ENOPROTOOPT || ret == -EACCES ||
        ret == -EOPNOTSUPP)
      goto binary_fallback_inp;
    return ret;
  }

  if (!rule_exists_in_hook(&info, ENTRIES_BLOB(base), NF_INET_LOCAL_IN, iface,
                           NULL, 0, 0, NULL)) {
    unsigned char rule_buf[XT_ALIGN(sizeof(struct ipt_entry)) +
                           XT_ALIGN(sizeof(struct xt_standard_target))];
    memset(rule_buf, 0, sizeof(rule_buf));

    struct ipt_entry *re = (struct ipt_entry *)rule_buf;
    struct xt_standard_target *st =
        (struct xt_standard_target *)(rule_buf +
                                      XT_ALIGN(sizeof(struct ipt_entry)));

    safe_strncpy(re->ip.iniface, iface, IFNAMSIZ);
    memset(re->ip.iniface_mask, 0xff, strlen(iface) + 1);
    re->target_offset = (__u16)XT_ALIGN(sizeof(struct ipt_entry));
    re->next_offset = (__u16)sizeof(rule_buf);

    st->target.u.target_size =
        (__u16)XT_ALIGN(sizeof(struct xt_standard_target));
    st->target.u.user.name[0] = '\0';
    st->target.u.user.revision = 0;
    st->verdict = -NF_ACCEPT - 1;

    ret = insert_rule_at_hook(fd, "filter", &info, ENTRIES_BLOB(base),
                              NF_INET_LOCAL_IN, rule_buf, sizeof(rule_buf));
    if (ret == 0) {
      ds_log("[IPT] INPUT -i %s ACCEPT inserted", iface);
    } else {
      ds_log("[IPT] Raw insert INPUT failed (ret=%d), binary fallback", ret);
      char *a[] = {"iptables", "-I",     "INPUT",
                   "1",        "-i",     (char *)(uintptr_t)iface,
                   "-j",       "ACCEPT", NULL};
      run_command_quiet(a);
    }
  } else {
    ds_log("[IPT] INPUT -i %s ACCEPT already present", iface);
  }

  free(base);
  close(fd);
  return 0;

binary_fallback_inp: {
  char *a[] = {"iptables", "-I",     "INPUT",
               "1",        "-i",     (char *)(uintptr_t)iface,
               "-j",       "ACCEPT", NULL};
  return run_command_quiet(a);
}
}

/* ---------------------------------------------------------------------------
 * Public API: ds_ipt_ensure_mss_clamp
 *
 * MSS clamping rule for TCP SYN packets — prevents MTU blackhole through
 * bridge + veth path.
 *
 * The raw socket API for this rule is disproportionately complex because it
 * requires two match extension payloads (xt_tcp + xt_TCPMSS with pmtu flag).
 * We use the iptables binary exclusively, with an existence check first to
 * remain idempotent.
 * ---------------------------------------------------------------------------*/

int ds_ipt_ensure_mss_clamp(void) {
  ds_log("[IPT] ensure_mss_clamp");

  char *check[] = {"iptables",
                   "-t",
                   "mangle",
                   "-C",
                   "POSTROUTING",
                   "-p",
                   "tcp",
                   "--tcp-flags",
                   "SYN,RST",
                   "SYN",
                   "-j",
                   "TCPMSS",
                   "--clamp-mss-to-pmtu",
                   NULL};
  if (run_command_quiet(check) == 0) {
    ds_log("[IPT] MSS clamp already present");
    return 0;
  }

  char *add[] = {"iptables",    "-t",
                 "mangle",      "-I",
                 "POSTROUTING", "1",
                 "-p",          "tcp",
                 "--tcp-flags", "SYN,RST",
                 "SYN",         "-j",
                 "TCPMSS",      "--clamp-mss-to-pmtu",
                 NULL};
  return run_command_quiet(add);
}

int ds_ipt_remove_iface_rules(const char *iface) {
  if (!iface || !iface[0])
    return 0;

  ds_log("[IPT] remove_iface_rules: iface=%s", iface);
  int fd = open_raw_socket();
  if (fd < 0) {
    char *a1[] = {
        "iptables", "-D",     "FORWARD", "-i", (char *)(uintptr_t)iface,
        "-j",       "ACCEPT", NULL};
    run_command_quiet(a1);
    char *a2[] = {
        "iptables", "-D",     "FORWARD", "-o", (char *)(uintptr_t)iface,
        "-j",       "ACCEPT", NULL};
    run_command_quiet(a2);
    char *a3[] = {"iptables", "-D",     "INPUT", "-i", (char *)(uintptr_t)iface,
                  "-j",       "ACCEPT", NULL};
    run_command_quiet(a3);
    return 0;
  }

  struct ipt_getinfo info;
  unsigned char *base = NULL;

  /* FORWARD */
  if (get_table(fd, "filter", &info, &base) == 0) {
    remove_matching_rules(fd, "filter", &info, ENTRIES_BLOB(base), 0, 0, iface);
    free(base);
  }

  /* INPUT */
  base = NULL;
  if (get_table(fd, "filter", &info, &base) == 0) {
    remove_matching_rules(fd, "filter", &info, ENTRIES_BLOB(base), 0, 0, iface);
    free(base);
  }

  close(fd);
  return 0;
}

/* ---------------------------------------------------------------------------
 * Public API: ds_ipt_remove_ds_rules
 *
 * Cleanly removes all rules Droidspaces inserted during NAT setup.
 * Safe to call even if container died unexpectedly.
 * ---------------------------------------------------------------------------*/

int ds_ipt_remove_ds_rules(void) {
  ds_log("[IPT] remove_ds_rules");
  probe_iptables_modules();

  int fd = open_raw_socket();

  uint32_t ds_src, ds_mask;
  parse_cidr(DS_DEFAULT_SUBNET, &ds_src, &ds_mask);

  /* 1. NAT table: remove our MASQUERADE rule */
  if (fd >= 0) {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    if (get_table(fd, "nat", &info, &base) == 0) {
      remove_matching_rules(fd, "nat", &info, ENTRIES_BLOB(base), ds_src,
                            ds_mask, NULL);
      free(base);
    }

    /* 2. Filter table: remove our bridge ACCEPT rules */
    base = NULL;
    if (get_table(fd, "filter", &info, &base) == 0) {
      /* Remove FORWARD rules */
      remove_matching_rules(fd, "filter", &info, ENTRIES_BLOB(base), 0, 0,
                            DS_NAT_BRIDGE);
      free(base);
    }
    /* 3. Filter table: remove our bridge INPUT rules (Fixed: was missing) */
    base = NULL;
    if (get_table(fd, "filter", &info, &base) == 0) {
      /* remove_matching_rules handles both iniface and outiface.
       * For INPUT, only iniface matters, which it checks. */
      remove_matching_rules(fd, "filter", &info, ENTRIES_BLOB(base), 0, 0,
                            DS_NAT_BRIDGE);
      free(base);
    }
    close(fd);
  } else {
    /* Binary fallback for cleanup */
    char *del_masq[] = {"iptables",
                        "-t",
                        "nat",
                        "-D",
                        "POSTROUTING",
                        "-s",
                        DS_DEFAULT_SUBNET,
                        "!",
                        "-d",
                        DS_DEFAULT_SUBNET,
                        "-j",
                        "MASQUERADE",
                        NULL};
    run_command_quiet(del_masq);
    char *del_fwd_in[] = {"iptables",    "-D", "FORWARD", "-i",
                          DS_NAT_BRIDGE, "-j", "ACCEPT",  NULL};
    run_command_quiet(del_fwd_in);
    char *del_fwd_out[] = {"iptables",    "-D", "FORWARD", "-o",
                           DS_NAT_BRIDGE, "-j", "ACCEPT",  NULL};
    run_command_quiet(del_fwd_out);
    char *del_inp[] = {"iptables",    "-D", "INPUT",  "-i",
                       DS_NAT_BRIDGE, "-j", "ACCEPT", NULL};
    run_command_quiet(del_inp);
  }

  /* 3. MSS clamp: binary only */
  {
    char *del_mss[] = {"iptables",
                       "-t",
                       "mangle",
                       "-D",
                       "POSTROUTING",
                       "-p",
                       "tcp",
                       "--tcp-flags",
                       "SYN,RST",
                       "SYN",
                       "-j",
                       "TCPMSS",
                       "--clamp-mss-to-pmtu",
                       NULL};
    run_command_quiet(del_mss);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Public API: ds_ipt_add_portforwards
 *
 * For each entry in cfg->port_forwards, inserts:
 *   -t nat    -I PREROUTING  -p <proto> --dport <host_port> -j DNAT
 *             --to-destination <container_ip>:<container_port>
 *   -t filter -I FORWARD     -p <proto> -d <container_ip>
 *             --dport <container_port> -j ACCEPT
 *
 * Binary fallback only — DNAT raw socket construction is disproportionately
 * complex for a feature that only fires at container start.
 * ---------------------------------------------------------------------------*/

int ds_ipt_add_portforwards(struct ds_config *cfg, const char *container_ip) {
  if (!cfg || cfg->port_forward_count <= 0 || !container_ip ||
      container_ip[0] == '\0')
    return 0;

  for (int i = 0; i < cfg->port_forward_count; i++) {
    struct ds_port_forward *pf = &cfg->port_forwards[i];

    char host_port_str[8], cont_port_str[8], to_dest[64];
    snprintf(host_port_str, sizeof(host_port_str), "%u", pf->host_port);
    snprintf(cont_port_str, sizeof(cont_port_str), "%u", pf->container_port);
    snprintf(to_dest, sizeof(to_dest), "%s:%u", container_ip,
             pf->container_port);

    ds_log("portforward: %s %s -> %s", pf->proto, host_port_str, to_dest);

    /* PREROUTING DNAT */
    char *dnat[] = {"iptables",         "-t",          "nat", "-I",
                    "PREROUTING",       "1",           "-p",  pf->proto,
                    "--dport",          host_port_str, "-j",  "DNAT",
                    "--to-destination", to_dest,       NULL};
    if (run_command_quiet(dnat) != 0)
      ds_warn("portforward: DNAT insert failed for port %s", host_port_str);

    /* FORWARD ACCEPT */
    char *fwd[] = {
        "iptables", "-I",          "FORWARD", "1",
        "-p",       pf->proto,     "-d",      (char *)(uintptr_t)container_ip,
        "--dport",  cont_port_str, "-j",      "ACCEPT",
        NULL};
    if (run_command_quiet(fwd) != 0)
      ds_warn("portforward: FORWARD insert failed for port %s", cont_port_str);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Public API: ds_ipt_remove_portforwards
 *
 * Mirrors ds_ipt_add_portforwards — deletes the DNAT + FORWARD rules.
 * Called from ds_net_cleanup() before ds_ipt_remove_ds_rules().
 * ---------------------------------------------------------------------------*/

int ds_ipt_remove_portforwards(struct ds_config *cfg) {
  if (!cfg || cfg->port_forward_count <= 0 || cfg->nat_container_ip[0] == '\0')
    return 0;

  const char *container_ip = cfg->nat_container_ip;

  for (int i = 0; i < cfg->port_forward_count; i++) {
    struct ds_port_forward *pf = &cfg->port_forwards[i];

    char host_port_str[8], cont_port_str[8], to_dest[64];
    snprintf(host_port_str, sizeof(host_port_str), "%u", pf->host_port);
    snprintf(cont_port_str, sizeof(cont_port_str), "%u", pf->container_port);
    snprintf(to_dest, sizeof(to_dest), "%s:%u", container_ip,
             pf->container_port);

    /* Delete PREROUTING DNAT */
    char *del_dnat[] = {"iptables",    "-t", "nat",     "-D",
                        "PREROUTING",  "-p", pf->proto, "--dport",
                        host_port_str, "-j", "DNAT",    "--to-destination",
                        to_dest,       NULL};
    run_command_quiet(del_dnat);

    /* Delete FORWARD ACCEPT */
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
  }

  return 0;
}
