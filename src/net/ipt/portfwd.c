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

/* Return-path MASQUERADE for a forward. It used to match every source, which
 * rewrote LAN clients to the gateway address and hid their real IPs from the
 * container (Pi-hole saw one client). Loopback clients and other containers
 * still need it: their replies would otherwise skip the host and get dropped.
 * LAN clients already route back through the host, so they keep their real
 * address. The loopback source is 127/8, not addrtype LOCAL: that is where the
 * OUTPUT-chain DNAT sends from, and it needs no xt_addrtype module. */
static const char *const pf_masq_src[3][4] = {
    {"-s", "127.0.0.0/8", NULL, NULL},
    {"-s", DS_DEFAULT_SUBNET, NULL, NULL},
    /* no source match: the rule older builds inserted, still removed so a
     * container started before an upgrade is cleaned up on stop */
    {NULL, NULL, NULL, NULL},
};

/* op is "-I" or "-D". Inserts skip the legacy row. Returns the number of
 * insert rules that went in; deletes return 0. */
static int pf_masq(const char *op, const char *proto, const char *container_ip,
                   const char *cont_port_str) {
  int insert = (strcmp(op, "-I") == 0);
  int added = 0;
  for (int i = 0; i < (insert ? 2 : 3); i++) {
    char *argv[24];
    int n = 0;
    argv[n++] = "iptables";
    argv[n++] = "-t";
    argv[n++] = "nat";
    argv[n++] = (char *)(uintptr_t)op;
    argv[n++] = "POSTROUTING";
    if (insert)
      argv[n++] = "1";
    argv[n++] = "-p";
    argv[n++] = (char *)(uintptr_t)proto;
    for (int j = 0; j < 4 && pf_masq_src[i][j]; j++)
      argv[n++] = (char *)(uintptr_t)pf_masq_src[i][j];
    argv[n++] = "-d";
    argv[n++] = (char *)(uintptr_t)container_ip;
    argv[n++] = "--dport";
    argv[n++] = (char *)(uintptr_t)cont_port_str;
    argv[n++] = "-j";
    argv[n++] = "MASQUERADE";
    argv[n] = NULL;
    if (run_command_quiet(argv) == 0)
      added++;
  }
  return insert ? added : 0;
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
      pf_masq("-D", proto, container_ip, cont_port_str);
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

    int masq_added = pf_masq("-I", pf->proto, container_ip, cont_port_str);
    if (masq_added != 2)
      ds_warn("portforward: POSTROUTING MASQUERADE %s for port %s",
              masq_added ? "incomplete" : "failed", cont_port_str);

    if (lo_ok)
      pf_state_append(state_f, "local_out", pf->proto, host_port_str, to_dest,
                      cont_port_str);
    /* Record even a partial insert: removal walks every source variant, so
     * one surviving rule is still cleaned up if the config changes. */
    if (masq_added)
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
    pf_masq("-D", pf->proto, container_ip, cont_port_str);
  }

  /* Nothing to sweep for a container that was booted without forwards: cfg
   * is the snapshot it booted with, and both cases the sweep exists for (an
   * older version that wrote no state file, a crash before the file was
   * written) leave the forwards in it. Without this every NAT stop paid four
   * sh pipelines for nothing. */
  if (!had_state && cfg->port_forward_count == 0)
    return 0;

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
