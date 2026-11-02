/*
 * Droidspaces v7 - High-performance Container Runtime
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
 *     path for port forwards.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <arpa/inet.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_nat.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_TCPMSS.h>
#include <linux/netfilter/xt_tcpudp.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

#include "net/ipt/internal.h"

static const struct xt_family xt_v4 = {
    AF_INET,
    IPPROTO_IP,
    sizeof(struct ipt_entry),
    offsetof(struct ipt_entry, target_offset),
    offsetof(struct ipt_entry, next_offset),
    4,
    "iptables",
};

const struct xt_family xt_v6 = {
    AF_INET6,
    IPPROTO_IPV6,
    sizeof(struct ip6t_entry),
    offsetof(struct ip6t_entry, target_offset),
    offsetof(struct ip6t_entry, next_offset),
    16,
    "ip6tables",
};

const struct xt_family *xt_family_of(int family) {
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

unsigned int ent_next(const struct xt_family *f, const void *e) {
  return ent_u16(e, f->off_next);
}

unsigned int ent_target_off(const struct xt_family *f, const void *e) {
  return ent_u16(e, f->off_target);
}

void entry_view(const struct xt_family *f, void *e, struct xt_ent_view *v) {
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
    v->proto = &ip->proto;
    v->flags = &ip->flags;
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
    v->proto = &ip->proto;
    v->flags = &ip->flags;
    v->invflags = &ip->invflags;
  }
}

/* Read-only walkers hold const blobs; the view is shared with the rule
 * builders, which write through it. */
void entry_view_ro(const struct xt_family *f, const void *e,
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
int fam_parse_cidr(const struct xt_family *f, const char *cidr, uint8_t net[16],
                   uint8_t mask[16]) {
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

int modules_probed = 0;

void probe_iptables_modules(void) {
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

int bin_rule(const struct xt_family *f, const char *op, const char *table,
             const char *chain, const char *const spec[]) {
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

int bin_ensure(const struct xt_family *f, const char *table, const char *chain,
               const char *const spec[]) {
  if (bin_rule(f, "-C", table, chain, spec) == 0)
    return 0;
  /* An exit status is positive and every caller tests for < 0, so a refused
   * rule used to read as success and NAT66 was announced with no MASQUERADE. */
  return bin_rule(f, "-I", table, chain, spec) == 0 ? 0 : -1;
}

/* Internal: get table info + entries blob via getsockopt
 *
 * Returns 0 on success.  *entries_out points to the allocated
 * ipt_get_entries struct; caller frees it.
 *
 * Convenience macro: ENTRIES_BLOB(base) → pointer to the raw rule bytes
 * inside the ipt_get_entries allocation. */

int get_table(const struct xt_family *f, int fd, const char *table_name,
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

/* Internal: open a raw socket (shared across public APIs) */

int open_raw_socket(const struct xt_family *f) {
  int fd = socket(f->af, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_RAW);
  if (fd < 0)
    ds_log("[IPT] Failed to open %s raw socket: %s", f->bin, strerror(errno));
  return fd;
}

/* Start a rule in buf: zeroed, offsets set for a target of target_sz bytes.
 * Returns the target slot. buf must hold XT_ALIGN(entry_sz) + target_sz. */
struct xt_entry_target *rule_begin(const struct xt_family *f,
                                   unsigned char *buf, size_t target_sz) {
  size_t hdr = XT_ALIGN(f->entry_sz);
  uint16_t toff = (uint16_t)hdr, next = (uint16_t)(hdr + target_sz);

  memset(buf, 0, hdr + target_sz);
  memcpy(buf + f->off_target, &toff, sizeof(toff));
  memcpy(buf + f->off_next, &next, sizeof(next));
  return (struct xt_entry_target *)(buf + hdr);
}
