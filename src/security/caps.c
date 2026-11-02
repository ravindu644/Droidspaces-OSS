/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/capability.h>
#include <sys/prctl.h>

/*
 * ds_apply_capability_hardening()
 *
 * Drops dangerous capabilities from the bounding set to reduce the container's
 * attack surface.
 *
 * In Standard Mode (hw_access=0), we drop several sensitive capabilities.
 * In Hardware Mode (hw_access=1), we preserve most to ensure full
 * low-level hardware access (USB, Serial, Bluetooth, Flashing).
 */
void ds_apply_capability_hardening(int hw_access, int privileged_mask,
                                   int sandboxing) {
  if (privileged_mask & DS_PRIV_NOCAPS) {
    ds_log("[SEC] --privileged=nocaps: skipping capability drops.");
    return;
  }
  /* Universal drops - even in hardware mode, there's no legitimate use
   * for CAP_SYS_MODULE inside a container (kernel module loading).
   * CAP_SYS_BOOT is intentionally preserved - it is required for in-container
   * reboot(2) to work inside a PID namespace without rebooting the host. */
  int universal_drops[] = {CAP_SYS_MODULE, -1};
  int total_dropped = 0;

  for (int i = 0; universal_drops[i] != -1; i++) {
    if (prctl(PR_CAPBSET_DROP, universal_drops[i], 0, 0, 0) < 0) {
      if (errno != EINVAL) {
        ds_warn("[SEC] Failed to drop universal cap %d: %s", universal_drops[i],
                strerror(errno));
      }
    } else {
      total_dropped++;
    }
  }

  if (hw_access) {
    ds_log("[SEC] Hardware Mode: preserved bounding set (dropped %d universal "
           "caps).",
           total_dropped);
    return;
  }

  /* Standard Hardening Tier: drop capabilities that affect host stability
   * or allow escaping the container's isolation. */
  int caps_to_drop[] = {
      CAP_SYS_RAWIO,       /* Raw hardware access (I/O ports, memory) */
      CAP_SYS_PTRACE,      /* Process tracing/injection across namespaces */
      CAP_SYS_PACCT,       /* Process accounting */
      CAP_MAC_ADMIN,       /* Mandatory Access Control policy modification */
      CAP_MAC_OVERRIDE,    /* Bypass MAC policies */
      CAP_WAKE_ALARM,      /* Affect host power management / wakeups */
      CAP_BLOCK_SUSPEND,   /* Affect host power management / sleep */
      CAP_AUDIT_READ,      /* Read kernel audit logs */
      CAP_DAC_READ_SEARCH, /* Bypass file read/directory search permissions -
                            * the other half of the Shocker escape: combined
                            * with open_by_handle_at it allows reading any
                            * file on the host outside the mount namespace. */
      -1};

  for (int i = 0; caps_to_drop[i] != -1; i++) {
    /* runc opens the ns links under /proc/<pid> of an init that already
     * switched uid, and root only gets ptrace-read on another uid's process
     * through this cap. */
    if (sandboxing && caps_to_drop[i] == CAP_SYS_PTRACE)
      continue;
    if (prctl(PR_CAPBSET_DROP, caps_to_drop[i], 0, 0, 0) < 0) {
      if (errno != EINVAL) {
        ds_warn("[SEC] Failed to drop cap %d: %s", caps_to_drop[i],
                strerror(errno));
      }
    } else {
      total_dropped++;
    }
  }

  ds_log("[SEC] Bounding set hardened (dropped %d caps).", total_dropped);
}
