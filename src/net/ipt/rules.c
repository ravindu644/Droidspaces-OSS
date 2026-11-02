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

static const char *const k_mss_spec[] = {
    "-p", "tcp",    "--tcp-flags",         "SYN,RST", "SYN",
    "-j", "TCPMSS", "--clamp-mss-to-pmtu", NULL};

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

  /* mangle: the MSS clamp, by the installer's own test: a TCPMSS target not
   * bound to an interface. An unreadable mangle table leaves the verdict
   * untouched rather than failing the probe - see the header comment. */
  if (present) {
    base = NULL;
    if (get_table(f, fd, "mangle", &info, &base) == 0) {
      present = rule_exists_in_hook(f, &info, ENTRIES_BLOB(base),
                                    NF_INET_POST_ROUTING, "", "", NULL, NULL,
                                    "TCPMSS");
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
  const struct xt_family *f = xt_family_of(AF_INET6);
  int fd = open_raw_socket(f);
  if (fd >= 0) {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    if (get_table(f, fd, "nat", &info, &base) == 0) {
      free(base);
      /* Revision 0 is the one raw_ensure_masquerade() inserts. */
      struct xt_get_revision rev = {.name = "MASQUERADE"};
      socklen_t len = sizeof(rev);
      int ret =
          getsockopt(fd, f->level, IP6T_SO_GET_REVISION_TARGET, &rev, &len);
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
  bin_rule(f, "-N", "nat", "ds-cap-masq", none);
  int ok = bin_rule(f, "-A", "nat", "ds-cap-masq", masq) == 0;
  bin_rule(f, "-F", "nat", "ds-cap-masq", none);
  bin_rule(f, "-X", "nat", "ds-cap-masq", none);
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
 * Called with DS_NAT_BRIDGE ("ds-br0"), or with the veth in bridgeless mode.
 * Routed traffic enters and leaves through the bridge device, so that is the
 * name FORWARD sees; bridge netfilter is off and never shows it the ports. */

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
 * Raw socket first, like every other rule: a TCPMSS target in mangle
 * POSTROUTING that is not bound to an interface counts as present, a VPN's
 * fixed --set-mss included, since it clamps our SYNs just as well; one scoped
 * to its own tunnel does not. Otherwise clamp_rule() goes in. The binary is
 * the fallback for kernels without the raw API. */

int ds_ipt_ensure_mss_clamp(int family) {
  const struct xt_family *f = xt_family_of(family);
  ds_log("[IPT] ensure_mss_clamp: %s", f->bin);

  int ret = -ENOTSUP;
  int fd = open_raw_socket(f);
  if (fd >= 0) {
    struct ipt_getinfo info;
    unsigned char *base = NULL;
    ret = get_table(f, fd, "mangle", &info, &base);
    if (ret == 0) {
      if (rule_exists_in_hook(f, &info, ENTRIES_BLOB(base),
                              NF_INET_POST_ROUTING, "", "", NULL, NULL,
                              "TCPMSS")) {
        ds_log("[IPT] %s TCPMSS clamp already present - skipping", f->bin);
      } else {
        unsigned char rule_buf[DS_XT_RULE_MAX];
        unsigned int sz = clamp_rule(f, rule_buf);
        ret = insert_rule_at_hook(f, fd, "mangle", &info, ENTRIES_BLOB(base),
                                  NF_INET_POST_ROUTING, rule_buf, sz);
      }
      free(base);
    }
    close(fd);
  }
  if (ret == 0) {
    ds_log("[IPT] %s TCPMSS clamp in place via raw socket API", f->bin);
    return 0;
  }

  ds_log("[IPT] %s TCPMSS raw path failed (ret=%d), using the binary", f->bin,
         ret);
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
  /* The clamp is the exception: when the raw path could read the table and
   * found none of ours, there is nothing the binary could find either, and
   * its spec would match a foreign clamp-to-pmtu rule that was never ours. */
  if (removed < 0 || (removed == 0 && !(iface_flags & DS_IPT_MATCH_CLAMP)))
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
  remove_rule(f, fd, "mangle", NF_INET_POST_ROUTING, "POSTROUTING", NULL, NULL,
              NULL, DS_IPT_MATCH_CLAMP, k_mss_spec);

  if (fd >= 0)
    close(fd);
  return 0;
}
