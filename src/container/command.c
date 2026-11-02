/*
 * Droidspaces v7 - High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The monitor command socket. Root inside a container is real root, so the
 * host never reads anything the guest can write. What a running container
 * booted with, and its init pid, come from its monitor instead: a host process
 * outside the container that holds that config in memory for the container's
 * whole life. LXC answers the same questions the same way, from the command
 * socket lxc-start serves (src/lxc/commands.c). */

#include "droidspaces.h"
#include <sys/socket.h>
#include <sys/un.h>

#include "container/internal.h"

#define DS_CMD_PREFIX "droidspaces/"
#define DS_CMD_SUFFIX "/command"
/* A booted config never comes near this; it only bounds what we allocate */
#define DS_CMD_REPLY_MAX (1024 * 1024)

/* droidspaces/<safe name>/command, or droidspaces/#<hash>/command for a name
 * that would not fit in sun_path. The name is never trusted from the socket
 * either way: discovery asks the monitor for its config, name included. */
static void cmd_sock_name(const char *name, char *out, size_t size) {
  char safe[256];
  struct sockaddr_un probe;
  sanitize_container_name(name, safe, sizeof(safe));
  if (strlen(DS_CMD_PREFIX) + strlen(safe) + strlen(DS_CMD_SUFFIX) <
      sizeof(probe.sun_path) - 1) {
    snprintf(out, size, DS_CMD_PREFIX "%s" DS_CMD_SUFFIX, safe);
    return;
  }
  uint64_t h = 1469598103934665603ULL; /* FNV-1a 64 */
  for (const unsigned char *c = (const unsigned char *)safe; *c; c++)
    h = (h ^ *c) * 1099511628211ULL;
  snprintf(out, size, DS_CMD_PREFIX "#%016llx" DS_CMD_SUFFIX,
           (unsigned long long)h);
}

static void set_timeouts(int fd, int seconds) {
  struct timeval tv = {seconds, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/* What the monitor publishes: the init pid, then the config this boot runs
 * with. NULL while no init of ours runs, and then nobody is answered. */
static pthread_mutex_t g_reply_lock = PTHREAD_MUTEX_INITIALIZER;
static char *g_reply;
static uint32_t g_reply_len;
static int g_listen = -1, g_conn = -1;
static char g_name[256];

static int cmd_listen(const char *name) {
  char sock[128];
  cmd_sock_name(name, sock, sizeof(sock));
  struct sockaddr_un addr;
  socklen_t alen = ds_abstract_addr(&addr, sock);
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return -1;
  if (bind(fd, (struct sockaddr *)&addr, alen) < 0 || listen(fd, 8) < 0) {
    int err = errno;
    close(fd);
    errno = err;
    return -1;
  }
  return fd;
}

void ds_cmd_publish(struct ds_config *cfg) {
  char *msg = NULL;
  uint32_t len = 0;
  if (cfg) {
    size_t n = 0;
    char *buf = ds_config_serialize(cfg, &n, NULL);
    uint32_t p =
        htonl(cfg->container_pid > 0 ? (uint32_t)cfg->container_pid : 0);
    if (buf && (msg = malloc(n + sizeof(p))) != NULL) {
      memcpy(msg, &p, sizeof(p));
      memcpy(msg + sizeof(p), buf, n);
      len = (uint32_t)(n + sizeof(p));
    }
    free(buf);
  }
  /* Bound here, not by the thread later, so the socket answers from the
   * moment there is an answer: a stop right after start or a reboot must
   * get this boot's config, not fall back to the caller's. */
  pthread_mutex_lock(&g_reply_lock);
  char *old = g_reply;
  g_reply = msg;
  g_reply_len = len;
  if (msg && g_listen < 0)
    g_listen = cmd_listen(g_name);
  pthread_mutex_unlock(&g_reply_lock);
  free(old);
}

static void serve_one(int lfd) {
  /* The listener does not block. The connection lives under the lock too,
   * so fork_child can drop it. */
  pthread_mutex_lock(&g_reply_lock);
  int c = g_conn = accept4(lfd, NULL, NULL, SOCK_CLOEXEC);
  pthread_mutex_unlock(&g_reply_lock);
  if (c < 0)
    return;
  set_timeouts(c, 1);
  uint8_t type;
  uint32_t len;
  if (ds_peer_authorized(c, NULL) && ds_recv_frame_hdr(c, &type, &len) == 0 &&
      len == 0 && type == DS_CMD_CONFIG) {
    pthread_mutex_lock(&g_reply_lock);
    uint32_t n = g_reply_len;
    char *copy = g_reply ? malloc(n) : NULL;
    if (copy)
      memcpy(copy, g_reply, n);
    pthread_mutex_unlock(&g_reply_lock);
    if (copy)
      ds_send_frame(c, DS_CMD_CONFIG, copy, n);
    free(copy);
  }
  pthread_mutex_lock(&g_reply_lock);
  close(c);
  g_conn = -1;
  pthread_mutex_unlock(&g_reply_lock);
}

/* The monitor's own thread for the command socket, so a monitor that is busy
 * (setting up a boot, reconciling a gateway, asking another monitor) still
 * answers at once. It holds the name only while a reply is published: once
 * this boot's init is gone the name is free for a successor's monitor, and
 * while a publish found the name taken it keeps trying. */
static void *serve_loop(void *arg) {
  (void)arg;
  int warned = 0;
  for (;;) {
    /* The listener is made and dropped under the lock, which fork also
     * takes, so a child never inherits one g_listen does not name. */
    int err = 0;
    pthread_mutex_lock(&g_reply_lock);
    int live = g_reply != NULL;
    if (live && g_listen < 0) {
      g_listen = cmd_listen(g_name);
      err = errno;
    } else if (!live && g_listen >= 0) {
      close(g_listen);
      g_listen = -1;
    }
    int fd = g_listen;
    pthread_mutex_unlock(&g_reply_lock);
    if (fd >= 0)
      warned = 0;
    else if (live && !warned++)
      write_monitor_debug_log(g_name, "Command socket busy (%s), retrying",
                              strerror(err));
    /* ponytail: polls, 100 ms while nothing is published (a boot's setup)
     * so a fresh publish is served at once, 500 ms while bound to see a
     * withdrawal or while a taken name is retried; a pipe from
     * ds_cmd_publish would let it sleep */
    if (fd < 0) {
      usleep(live ? 500000 : 100000);
      continue;
    }
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, 500) > 0 && (p.revents & POLLIN))
      serve_one(fd);
  }
  return NULL;
}

/* The intermediate a monitor forks never execs, so CLOEXEC would not drop
 * the listener there: it would hold the name for as long as init lives. */
static void fork_prepare(void) { pthread_mutex_lock(&g_reply_lock); }
static void fork_parent(void) { pthread_mutex_unlock(&g_reply_lock); }
static void fork_child(void) {
  if (g_listen >= 0)
    close(g_listen);
  if (g_conn >= 0)
    close(g_conn);
  g_listen = g_conn = -1;
  pthread_mutex_unlock(&g_reply_lock);
}

void ds_cmd_serve_start(const char *name) {
  safe_strncpy(g_name, name, sizeof(g_name));
  pthread_t tid;
  if (pthread_atfork(fork_prepare, fork_parent, fork_child) == 0 &&
      ds_thread_create(&tid, serve_loop, NULL) == 0)
    pthread_detach(tid);
}

/* The next listener on an abstract name in the kernel's table of unix
 * sockets: its name without the '@', and its inode. Lines are Num RefCount
 * Protocol Flags Type St Inode Path. Listening sockets carry __SO_ACCEPTCON
 * in Flags, 00010000; their accepted connections show the same path and are
 * skipped. Only our network namespace's sockets are listed, and an abstract
 * name has one listener in it. */
static int next_listener(FILE *f, char path[256], unsigned long *ino) {
  char line[512], flags[16];
  while (fgets(line, sizeof(line), f))
    if (sscanf(line, "%*s %*s %*s %15s %*s %*s %lu %255s", flags, ino, path) ==
            3 &&
        strcmp(flags, "00010000") == 0 && path[0] == '@') {
      memmove(path, path + 1, strlen(path));
      return 1;
    }
  return 0;
}

/* The inode of the socket listening on the abstract name sock, or 0 */
static unsigned long listener_inode(const char *sock) {
  FILE *f = fopen("/proc/net/unix", "re");
  if (!f)
    return 0;
  char path[256];
  unsigned long ino, found = 0;
  while (!found && next_listener(f, path, &ino))
    if (strcmp(path, sock) == 0)
      found = ino;
  fclose(f);
  return found;
}

/* 1 if pid holds the socket with inode ino among its open fds */
static int holds_socket(pid_t pid, unsigned long ino) {
  char dir[64], want[64];
  snprintf(dir, sizeof(dir), "/proc/%d/fd", (int)pid);
  snprintf(want, sizeof(want), "socket:[%lu]", ino);
  DIR *d = ino ? opendir(dir) : NULL;
  if (!d)
    return 0;
  int held = 0;
  struct dirent *e;
  while (!held && (e = readdir(d)) != NULL) {
    char path[320], link[64];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    ssize_t n = readlink(path, link, sizeof(link) - 1);
    if (n > 0) {
      link[n] = '\0';
      held = strcmp(link, want) == 0;
    }
  }
  closedir(d);
  return held;
}

/* 1 if the listener behind connected fd is a monitor of ours. SO_PEERCRED
 * gives the pid that called listen, recorded then and never updated, so on
 * its own it proves nothing: a container sharing the host network namespace
 * can bind the name, let that process exit and keep the socket in a child
 * until a host root process gets the pid. So the pid must be a [ds-monitor]
 * in our pid namespace and must hold that very socket now. */
static pid_t server_is_monitor(int fd, const char *sock, unsigned long ino) {
  struct ucred cred;
  socklen_t len = sizeof(cred);
  return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0 &&
                 cred.uid == 0 && is_ds_monitor(cred.pid) &&
                 holds_socket(cred.pid, ino ? ino : listener_inode(sock))
             ? cred.pid
             : 0;
}

static pid_t reply_pid(const char *buf) {
  uint32_t p;
  memcpy(&p, buf, sizeof(p));
  return (pid_t)ntohl(p);
}

/* Ask the monitor behind the abstract socket sock for its reply. ino is the
 * listener's inode if the caller already read it from /proc/net/unix, else
 * 0. *out is malloc'd and NUL-terminated. */
static int cmd_query(const char *sock, unsigned long ino, char **out,
                     uint32_t *len) {
  struct sockaddr_un addr;
  socklen_t alen = ds_abstract_addr(&addr, sock);
  /* Non-blocking only for connect: a listener with a full backlog, which a
   * squatter can leave, fails it at once instead of after the timeout */
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return -1;
  /* The monitor answers from its own thread at once; one that does not must
   * not stall a caller that walks every container. */
  set_timeouts(fd, 1);
  /* The 5-byte request frame, sent with MSG_NOSIGNAL: a monitor that went
   * away since connect must not take a direct CLI run down with SIGPIPE. */
  uint8_t req[5] = {DS_CMD_CONFIG, 0, 0, 0, 0}, rtype;
  pid_t mon = 0;
  int ok = connect(fd, (struct sockaddr *)&addr, alen) == 0 &&
           fcntl(fd, F_SETFL, 0) == 0 &&
           (mon = server_is_monitor(fd, sock, ino)) > 0 &&
           send(fd, req, sizeof(req), MSG_NOSIGNAL) == (ssize_t)sizeof(req) &&
           ds_recv_frame_hdr(fd, &rtype, len) == 0 && rtype == DS_CMD_CONFIG &&
           *len > sizeof(uint32_t) && *len <= DS_CMD_REPLY_MAX &&
           (*out = malloc(*len + 1)) != NULL;
  if (ok && ds_read_exact(fd, *out, *len) < 0) {
    free(*out);
    ok = 0;
  }
  close(fd);
  if (!ok)
    return -1;
  /* An answer counts only while its init still runs under this monitor
   * (monitor, intermediate, init). A monitor that has not yet noticed its
   * init died would hand out a dead boot's config, or a pid already reused
   * by some other container's init. */
  pid_t pid = reply_pid(*out);
  if (pid <= 0 || !is_container_init(pid) || proc_ppid(proc_ppid(pid)) != mon) {
    free(*out);
    return -1;
  }
  (*out)[*len] = '\0';
  return 0;
}

static int cmd_config(const char *sock, unsigned long ino,
                      struct ds_config *cfg, pid_t *pid) {
  char *buf;
  uint32_t len;
  if (cmd_query(sock, ino, &buf, &len) < 0)
    return -1;
  if (pid)
    *pid = reply_pid(buf);
  FILE *f = fmemopen(buf + sizeof(uint32_t), len - sizeof(uint32_t), "r");
  int ret = f ? ds_config_load_file(f, cfg) : -1;
  if (f)
    fclose(f);
  free(buf);
  return ret;
}

int ds_cmd_get_config(const char *name, struct ds_config *cfg, pid_t *pid) {
  char sock[128];
  cmd_sock_name(name, sock, sizeof(sock));
  return cmd_config(sock, 0, cfg, pid);
}

pid_t ds_cmd_get_pid(const char *name) {
  char sock[128], *buf;
  uint32_t len;
  cmd_sock_name(name, sock, sizeof(sock));
  if (cmd_query(sock, 0, &buf, &len) < 0)
    return 0;
  pid_t pid = reply_pid(buf);
  free(buf);
  return pid;
}

/* Every monitor that is listening, found the way LXC finds running
 * containers: from the kernel's table of unix sockets, /proc/net/unix, never
 * from anything inside a container. visit gets the booted config each monitor
 * sends (name included) and the init pid it reports. Returns how many it
 * visited. */
int ds_cmd_for_each(void (*visit)(struct ds_config *booted, pid_t pid,
                                  void *arg),
                    void *arg) {
  FILE *f = fopen("/proc/net/unix", "re");
  if (!f)
    return 0;
  char sock[256];
  unsigned long ino;
  int visited = 0;
  while (next_listener(f, sock, &ino)) {
    size_t n = strlen(sock), sl = strlen(DS_CMD_SUFFIX);
    if (strncmp(sock, DS_CMD_PREFIX, strlen(DS_CMD_PREFIX)) != 0 || n <= sl ||
        strcmp(sock + n - sl, DS_CMD_SUFFIX) != 0)
      continue;

    struct ds_config booted;
    pid_t pid = 0;
    ds_config_init(&booted);
    if (cmd_config(sock, ino, &booted, &pid) == 0 && booted.container_name[0]) {
      visit(&booted, pid, arg);
      visited++;
    }
    ds_config_free(&booted);
  }
  fclose(f);
  return visited;
}
