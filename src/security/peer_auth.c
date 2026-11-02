/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <grp.h>
#include <pwd.h>
#include <sys/socket.h>

int ds_peer_in_pidns(pid_t peer_pid) {
  /* This is a trust boundary: unless we can positively confirm the peer shares
   * our PID namespace, deny. A pid of 0 means the peer is not translatable into
   * our namespace (i.e. outside it), and a failed readlink (e.g. the pid died
   * and was reaped) means we cannot prove membership -- both must fail closed.
   * Failing open here lets a caller recycle a dead pid to bypass the guard. */
  if (peer_pid <= 0)
    return 0; /* not translatable into our ns -> deny */

  char path[64], self_ns[64], peer_ns[64];
  ssize_t sn = readlink("/proc/self/ns/pid", self_ns, sizeof(self_ns) - 1);
  /* Our own ns/pid is missing while ns/mnt (unconditional in procfs since
   * 3.8) is there: this kernel was built without CONFIG_PID_NS. There is
   * exactly one PID namespace, so the peer cannot be anywhere else and no
   * container exists to hide it in. That is a positive answer, not "cannot
   * confirm". Denying here rejects every client before it sends its request,
   * and the daemon closing mid-write is what the client saw as a silent exit
   * 141; the requirement probe in the worker is what reports the missing
   * feature. The peer-path ENOENT below (dead, recycled pid) still denies. */
  if (sn < 0 && errno == ENOENT && access("/proc/self/ns/mnt", F_OK) == 0)
    return 1;
  snprintf(path, sizeof(path), "/proc/%d/ns/pid", (int)peer_pid);
  ssize_t pn = readlink(path, peer_ns, sizeof(peer_ns) - 1);
  if (sn <= 0 || pn <= 0)
    return 0; /* cannot confirm -> deny */

  self_ns[sn] = '\0';
  peer_ns[pn] = '\0';
  return strcmp(self_ns, peer_ns) == 0;
}

int ds_peer_authorized(int fd, const char *group_name) {
#ifdef SO_PEERCRED
  struct ucred cred;
  socklen_t clen = sizeof(cred);
  memset(&cred, 0, sizeof(cred));
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &clen) < 0 ||
      clen != sizeof(cred))
    return 0;

  /* Abstract sockets are network-namespace scoped, so reject peers outside our
   * PID namespace (host-net container guard) before any uid/group check. */
  if (!ds_peer_in_pidns(cred.pid))
    return 0;

  if (cred.uid == 0)
    return 1;

  if (!group_name)
    return 0;
  struct group *gr = getgrnam(group_name);
  struct passwd *pw = getpwuid(cred.uid);
  if (!gr || !pw)
    return 0;

  /* getgrouplist() returns -1 when the user is in more than ngroups groups,
   * setting ngroups to the required count while filling only the first slots.
   * Reallocate and retry so a member with many supplementary groups is still
   * authorized instead of being read out of bounds / wrongly denied. */
  int ngroups = 64;
  gid_t stackgroups[64];
  gid_t *groups = stackgroups;
  gid_t *heapgroups = NULL;
  int authorized = 0;
  if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) < 0) {
    heapgroups = malloc(sizeof(gid_t) * (size_t)ngroups);
    if (heapgroups &&
        getgrouplist(pw->pw_name, pw->pw_gid, heapgroups, &ngroups) >= 0)
      groups = heapgroups;
    else
      ngroups = 0; /* fail closed */
  }
  for (int i = 0; i < ngroups; i++) {
    if (groups[i] == gr->gr_gid) {
      authorized = 1;
      break;
    }
  }
  free(heapgroups);
  return authorized;
#else
  (void)fd;
  (void)group_name;
  return 0;
#endif
}
