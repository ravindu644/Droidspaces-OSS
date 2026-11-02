/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"
#include <arpa/inet.h>
#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "daemon/internal.h"

/*
 * g_self_path is populated once during daemon startup (after daemonize()).
 * Subsequent fork()+reexec() children inherit a copy of this path.
 * After an atomic `mv` replaces the binary on disk, this path resolves to
 * the NEW binary - so all future sessions transparently use the updated code
 * without restarting the daemon.
 */
char g_self_path[PATH_MAX];

void send_exit(int fd, int code) {
  uint32_t nc = htonl((uint32_t)code);
  ds_send_frame(fd, MSG_EXIT, &nc, 4);
}

/* request parsing */

void free_req(ds_req_t *r) {
  for (int i = 0; i < r->argc; i++) {
    if (r->argv[i]) {
      free(r->argv[i]);
      r->argv[i] = NULL;
    }
  }
}

int recv_req(int fd, ds_req_t *r) {
  memset(r, 0, sizeof(*r));
  uint32_t nf, na;
  if (ds_read_exact(fd, &nf, 4) < 0 || ds_read_exact(fd, &na, 4) < 0)
    return -1;
  r->flags = ntohl(nf);
  uint32_t argc = ntohl(na);
  if (!argc || argc > DS_MAX_ARGC)
    return -1;

  for (uint32_t i = 0; i < argc; i++) {
    uint32_t nl;
    if (ds_read_exact(fd, &nl, 4) < 0)
      return -1;
    uint32_t al = ntohl(nl);
    if (al > DS_MAX_ARG)
      return -1;
    r->argv[i] = (char *)malloc((size_t)al + 1);
    if (!r->argv[i])
      return -1;
    if (al && ds_read_exact(fd, r->argv[i], al) < 0)
      return -1;
    r->argv[i][al] = '\0';
    r->argc++; /* safely increment once we actually have the string */
  }
  r->argv[r->argc] = NULL;

  if (r->flags & REQ_FLAG_PTY) {
    uint16_t ws[2];
    if (ds_read_exact(fd, ws, 4) < 0)
      return -1;
    r->rows = ntohs(ws[0]);
    r->cols = ntohs(ws[1]);
    if (!r->rows)
      r->rows = 24;
    if (!r->cols)
      r->cols = 80;
  }
  return 0;
}

/* re-execute the droidspaces binary */

void reexec(char **argv) {
  /*
   * Use the path pinned at daemon startup. After an atomic mv swap,
   * g_self_path resolves to the new binary automatically. If for some
   * reason it was never set, fall back to the symlink (which may have
   * (deleted) but is better than nothing).
   */
  const char *path = (g_self_path[0] != '\0') ? g_self_path : "/proc/self/exe";
  execv(path, argv);
  _exit(127);
}

char **make_exec_argv(ds_req_t *r) {
  char **av = (char **)malloc((size_t)(r->argc + 2) * sizeof(char *));
  if (!av)
    return NULL;
  av[0] = (char *)"droidspaces";
  for (int i = 0; i < r->argc; i++)
    av[i + 1] = r->argv[i];
  av[r->argc + 1] = NULL;
  return av;
}
