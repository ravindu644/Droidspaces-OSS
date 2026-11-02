/*
 * Droidspaces v7 - High-performance Container Runtime
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

#include "internal.h"

int hook_contains_offset(const struct ipt_getinfo *info, unsigned int hook_id,
                         unsigned int offset) {
  if (!info || hook_id >= NF_INET_NUMHOOKS ||
      !(info->valid_hooks & (1u << hook_id)))
    return 0;
  return offset >= info->hook_entry[hook_id] &&
         offset < info->underflow[hook_id];
}

int iface_empty(const char *iface, const unsigned char *mask) {
  for (int i = 0; i < IFNAMSIZ; i++) {
    if (iface[i] != '\0' || mask[i] != 0)
      return 0;
  }
  return 1;
}

int iface_exact_match(const char *rule_iface, const unsigned char *rule_mask,
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

int target_is_accept(const struct xt_entry_target *t) {
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
const char *target_label(const struct xt_entry_target *t) {
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

int rule_exists_in_hook(const struct xt_family *f,
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
    /* NULL: any interface. "": the rule must not be bound to one. */
    if (match && iface_in && strncmp(v.iniface, iface_in, IFNAMSIZ) != 0)
      match = 0;
    if (match && iface_out && strncmp(v.outiface, iface_out, IFNAMSIZ) != 0)
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
struct xt_standard_target *entry_standard_target(const struct xt_family *f,
                                                 unsigned char *e,
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

/* Build "-p tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu" in
 * buf, which must hold DS_XT_RULE_MAX bytes. Returns the rule's size. The
 * kernel accepts a clamp only on a rule that matches TCP SYNs, which is what
 * the tcp match says. */
unsigned int clamp_rule(const struct xt_family *f, unsigned char *buf) {
  size_t hdr = XT_ALIGN(f->entry_sz);
  uint16_t toff = (uint16_t)(hdr + DS_XT_CLAMP_MATCH_SZ);
  uint16_t next = (uint16_t)(toff + DS_XT_CLAMP_TARGET_SZ);

  memset(buf, 0, next);
  memcpy(buf + f->off_target, &toff, sizeof(toff));
  memcpy(buf + f->off_next, &next, sizeof(next));

  struct xt_ent_view v;
  entry_view(f, buf, &v);
  *v.proto = IPPROTO_TCP;
  if (f->af == AF_INET6)
    *v.flags |= IP6T_F_PROTO; /* ip6t only looks at proto when told to */

  struct xt_entry_match *m = (struct xt_entry_match *)(buf + hdr);
  m->u.match_size = DS_XT_CLAMP_MATCH_SZ;
  safe_strncpy(m->u.user.name, "tcp", sizeof(m->u.user.name));
  struct xt_tcp *tcp = (struct xt_tcp *)m->data;
  tcp->spts[1] = tcp->dpts[1] = 0xffff; /* any port */
  tcp->flg_mask = 0x02 | 0x04;          /* SYN,RST */
  tcp->flg_cmp = 0x02;                  /* SYN */

  struct xt_entry_target *t = (struct xt_entry_target *)(buf + toff);
  t->u.target_size = DS_XT_CLAMP_TARGET_SZ;
  safe_strncpy(t->u.user.name, "TCPMSS", sizeof(t->u.user.name));
  ((struct xt_tcpmss_info *)t->data)->mss = XT_TCPMSS_CLAMP_PMTU;
  return next;
}

/* Is this entry the clamp clamp_rule() builds, byte for byte in the parts
 * that matter? Used by the remover so that only our rule goes, never a VPN's
 * TCPMSS rule that happens to share the chain. */
int is_our_clamp(const struct xt_family *f, const unsigned char *e) {
  unsigned int toff = ent_target_off(f, e);
  if (toff != XT_ALIGN(f->entry_sz) + DS_XT_CLAMP_MATCH_SZ)
    return 0;
  struct xt_ent_view v;
  entry_view_ro(f, e, &v);
  if (*v.proto != IPPROTO_TCP || !iface_empty(v.iniface, v.in_mask) ||
      !iface_empty(v.outiface, v.out_mask))
    return 0;

  const struct xt_entry_match *m =
      (const struct xt_entry_match *)(e + XT_ALIGN(f->entry_sz));
  const struct xt_tcp *tcp = (const struct xt_tcp *)m->data;
  if (m->u.match_size != DS_XT_CLAMP_MATCH_SZ ||
      strcmp(m->u.user.name, "tcp") != 0 || tcp->flg_mask != (0x02 | 0x04) ||
      tcp->flg_cmp != 0x02 || tcp->invflags != 0)
    return 0;

  const struct xt_entry_target *t = (const struct xt_entry_target *)(e + toff);
  return t->u.target_size == DS_XT_CLAMP_TARGET_SZ &&
         strcmp(t->u.user.name, "TCPMSS") == 0 &&
         ((const struct xt_tcpmss_info *)t->data)->mss == XT_TCPMSS_CLAMP_PMTU;
}
