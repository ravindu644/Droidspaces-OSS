/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DS_NET_IPT_INTERNAL_H
#define DS_NET_IPT_INTERNAL_H

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

/* The match fields of one entry, whichever family it belongs to. This is the
 * only place that knows the two IP header layouts. */
struct xt_ent_view {
  char *iniface, *outiface;
  unsigned char *in_mask, *out_mask;
  void *src, *smsk, *dst, *dmsk;
  uint16_t *proto;
  uint8_t *flags, *invflags;
};

#define ENTRIES_BLOB(base)                                                     \
  ((unsigned char *)((struct ipt_get_entries *)(base))->entrytable)

#define DS_IPT_MATCH_IN 0x01u
#define DS_IPT_MATCH_OUT 0x02u
#define DS_IPT_MATCH_CLAMP 0x04u /* our MSS clamp, see clamp_rule() */

/* The MSS clamp is [entry][tcp match][TCPMSS target]: the one rule we build
 * with a match extension. */
#define DS_XT_CLAMP_MATCH_SZ                                                   \
  XT_ALIGN(sizeof(struct xt_entry_match) + sizeof(struct xt_tcp))
#define DS_XT_CLAMP_TARGET_SZ                                                  \
  XT_ALIGN(sizeof(struct xt_entry_target) + sizeof(struct xt_tcpmss_info))
#define DS_XT_NAT_TARGET_SZ                                                    \
  XT_ALIGN(sizeof(struct xt_entry_target) + sizeof(struct nf_nat_range))

/* Big enough for any rule we build, in either family. */
#define DS_XT_RULE_MAX                                                         \
  (XT_ALIGN(sizeof(struct ip6t_entry)) +                                       \
   (DS_XT_CLAMP_MATCH_SZ + DS_XT_CLAMP_TARGET_SZ > DS_XT_NAT_TARGET_SZ         \
        ? DS_XT_CLAMP_MATCH_SZ + DS_XT_CLAMP_TARGET_SZ                         \
        : DS_XT_NAT_TARGET_SZ))

extern const struct xt_family xt_v6;
extern int modules_probed;

const struct xt_family *xt_family_of(int family);
unsigned int ent_next(const struct xt_family *f, const void *e);
unsigned int ent_target_off(const struct xt_family *f, const void *e);
void entry_view(const struct xt_family *f, void *e, struct xt_ent_view *v);
void entry_view_ro(const struct xt_family *f, const void *e,
                   struct xt_ent_view *v);
int fam_parse_cidr(const struct xt_family *f, const char *cidr, uint8_t net[16],
                   uint8_t mask[16]);
void probe_iptables_modules(void);
int bin_rule(const struct xt_family *f, const char *op, const char *table,
             const char *chain, const char *const spec[]);
int bin_ensure(const struct xt_family *f, const char *table, const char *chain,
               const char *const spec[]);
int get_table(const struct xt_family *f, int fd, const char *table_name,
              struct ipt_getinfo *info, unsigned char **entries_out);
int open_raw_socket(const struct xt_family *f);
struct xt_entry_target *rule_begin(const struct xt_family *f,
                                   unsigned char *buf, size_t target_sz);

int hook_contains_offset(const struct ipt_getinfo *info, unsigned int hook_id,
                         unsigned int offset);
int iface_empty(const char *iface, const unsigned char *mask);
int iface_exact_match(const char *rule_iface, const unsigned char *rule_mask,
                      const char *iface);
int target_is_accept(const struct xt_entry_target *t);
const char *target_label(const struct xt_entry_target *t);
int rule_exists_in_hook(const struct xt_family *f,
                        const struct ipt_getinfo *info,
                        const unsigned char *blob, unsigned int hook_id,
                        const char *iface_in, const char *iface_out,
                        const void *src, const void *src_mask,
                        const char *target_name);
struct xt_standard_target *entry_standard_target(const struct xt_family *f,
                                                 unsigned char *e,
                                                 unsigned int next);
unsigned int clamp_rule(const struct xt_family *f, unsigned char *buf);
int is_our_clamp(const struct xt_family *f, const unsigned char *e);

int insert_rule_at_hook(const struct xt_family *f, int fd,
                        const char *table_name, struct ipt_getinfo *info_in,
                        unsigned char *blob_in, unsigned int hook_id,
                        const void *new_rule, unsigned int new_rule_sz);
int remove_matching_rules(const struct xt_family *f, int fd,
                          const char *table_name, struct ipt_getinfo *info_in,
                          unsigned char *blob_in, unsigned int hook_id,
                          const void *match_src, const void *match_mask,
                          const char *match_iface, unsigned int iface_flags);

#endif /* DS_NET_IPT_INTERNAL_H */
