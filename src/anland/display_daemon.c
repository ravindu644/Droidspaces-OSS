/*
 * Anland 5.x display broker.
 *
 * Vendored from https://github.com/superturtlee/anland, branch legacy,
 * libdisplay_daemon/display_daemon.c, and adapted for Droidspaces:
 *
 *   - the two roles are authenticated. The consumer must be root: the Anland
 *     app connects through its su helper, which hands the fd over and exits,
 *     so only the uid can be checked, never the pid. The producer must come
 *     from inside a container. Anyone else is dropped.
 *   - a client gets a read and write timeout, so a peer that connects and
 *     says nothing cannot wedge the single-threaded loop.
 *   - every fd a message carries is either deposited or closed, and the fd
 *     set is accepted and delivered at the same count.
 *   - a client dropped during an epoll batch is freed after the batch, so a
 *     stale event cannot land on a new client that reuses the address.
 *
 * Logging is plain stderr on purpose: this code runs in the forked daemon,
 * whose stderr is the log relay pipe that writes Logs/<name>/anland. ds_log
 * would write to the launcher's log file through a descriptor the child has
 * closed.
 *
 * Copyright (C) 2026 the Anland authors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspaces.h"

#include "display_daemon.h"
#include "protocol.h"

#define MAX_EVENTS 16
/* Consumer fd set: { buf_ready, fence, data, shm, audio }. */
#define MAX_FDS 5
/* ponytail: a per-client timeout, not a non-blocking state machine. A hostile
 * peer can still hold the loop for this long per connection; it can only be a
 * process in the container whose own display it would be stalling. */
#define CLIENT_IO_TIMEOUT_S 2

#define dlog(...) fprintf(stderr, "daemon: " __VA_ARGS__)

struct client {
  int fd;
};

struct daemon_ctx {
  struct client *consumer;
  struct client *producer;
  int epoll_fd;
  int listen_fd;
  volatile sig_atomic_t running;

  struct screen_info screen;
  int has_screen;

  int fds[MAX_FDS];
  int fd_count;

  int producer_waiting_screen;
  int producer_waiting_fds;

  /* Clients dropped during the current batch; freed once it is over. */
  struct client *dead[MAX_EVENTS * 2];
  int ndead;

  char sock_path[108];
};

static void close_fds(int *fds, int count) {
  for (int i = 0; i < count; i++)
    close(fds[i]);
}

static void clear_deposit(daemon_ctx *ctx) {
  close_fds(ctx->fds, ctx->fd_count);
  ctx->fd_count = 0;
}

static int peer_cred(int fd, struct ucred *cred) {
  socklen_t clen = sizeof(*cred);
  return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, cred, &clen) == 0 &&
         clen == sizeof(*cred);
}

/* The credentials are captured at connect time, so they outlive the su
 * helper. A pid namespace check is impossible here for the same reason: the
 * helper is gone before the hello arrives. Container root can pass this gate,
 * but it already owns the desktop behind the socket. */
static int peer_is_root(int fd) {
  struct ucred cred;
  return peer_cred(fd, &cred) && cred.uid == 0;
}

/* Only a positive answer counts: the peer's pid must resolve and its PID
 * namespace must differ from ours. Anything unreadable is not a container. */
static int peer_in_container(int fd) {
  struct ucred cred;
  if (!peer_cred(fd, &cred) || cred.pid <= 0)
    return 0;
  char path[64], self_ns[64], peer_ns[64];
  ssize_t sn = readlink("/proc/self/ns/pid", self_ns, sizeof(self_ns) - 1);
  snprintf(path, sizeof(path), "/proc/%d/ns/pid", (int)cred.pid);
  ssize_t pn = readlink(path, peer_ns, sizeof(peer_ns) - 1);
  if (sn <= 0 || pn <= 0)
    return 0;
  self_ns[sn] = '\0';
  peer_ns[pn] = '\0';
  return strcmp(self_ns, peer_ns) != 0;
}

static int recv_all(int fd, void *buf, size_t len) {
  uint8_t *p = buf;
  size_t got = 0;
  while (got < len) {
    ssize_t n = recv(fd, p + got, len - got, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      return -1;
    }
    got += (size_t)n;
  }
  return 0;
}

/* Reads one control message with whatever fds ride on it. The caller owns
 * every fd in fds[] and must deposit or close each one. */
static int read_msg(int fd, struct ctrl_msg *hdr, int *fds, int *nfds,
                    struct screen_info *payload) {
  ssize_t n = ds_recv_fds(fd, hdr, sizeof(*hdr), fds, MAX_FDS, nfds);
  if (n != (ssize_t)sizeof(*hdr))
    goto fail;
  if (hdr->size > sizeof(*payload))
    goto fail;
  if (hdr->size > 0 && recv_all(fd, payload, hdr->size) < 0)
    goto fail;
  return 0;
fail:
  close_fds(fds, *nfds);
  *nfds = 0;
  return -1;
}

static int send_ctrl(int fd, uint32_t type) {
  struct ctrl_msg msg = {.type = type, .size = 0};
  return write_all(fd, &msg, sizeof(msg)) == (ssize_t)sizeof(msg) ? 0 : -1;
}

static int send_screen_info(daemon_ctx *ctx, int fd) {
  uint8_t buf[sizeof(struct ctrl_msg) + sizeof(struct screen_info)];
  struct ctrl_msg hdr = {.type = CTRL_MSG_SCREEN_INFO,
                         .size = sizeof(struct screen_info)};
  memcpy(buf, &hdr, sizeof(hdr));
  memcpy(buf + sizeof(hdr), &ctx->screen, sizeof(ctx->screen));
  return write_all(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf) ? 0 : -1;
}

static void try_deliver_fds(daemon_ctx *ctx) {
  if (!ctx->producer || ctx->fd_count != MAX_FDS) {
    ctx->producer_waiting_fds = 1;
    return;
  }
  struct ctrl_msg msg = {.type = CTRL_MSG_FDS_READY, .size = 0};
  if (ds_send_fds(ctx->producer->fd, &msg, sizeof(msg), ctx->fds,
                  ctx->fd_count) < 0) {
    dlog("failed to send fds to producer\n");
    ctx->producer_waiting_fds = 1;
    return;
  }
  if (ctx->consumer)
    send_ctrl(ctx->consumer->fd, CTRL_MSG_FDS_READY);
  clear_deposit(ctx);
  ctx->producer_waiting_fds = 0;
  dlog("fds delivered to producer\n");
}

/* Clears the client's role and closes it. The memory stays until the batch
 * ends, so the pointer in any event still queued cannot alias a new client. */
static void drop_client(daemon_ctx *ctx, struct client *c) {
  if (!c)
    return;
  if (c == ctx->consumer) {
    dlog("consumer disconnected\n");
    ctx->consumer = NULL;
    /* The deposit belonged to that consumer; a producer must never get it. */
    clear_deposit(ctx);
  } else if (c == ctx->producer) {
    dlog("producer disconnected\n");
    ctx->producer = NULL;
    ctx->producer_waiting_screen = 0;
    ctx->producer_waiting_fds = 0;
  }
  epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, c->fd, NULL);
  close(c->fd);
  if (ctx->ndead < (int)(sizeof(ctx->dead) / sizeof(ctx->dead[0])))
    ctx->dead[ctx->ndead++] = c;
  else
    free(c);
}

static void free_dead(daemon_ctx *ctx) {
  for (int i = 0; i < ctx->ndead; i++)
    free(ctx->dead[i]);
  ctx->ndead = 0;
}

/* A consumer hello, first or repeated, carries the whole fd set or nothing. */
static void deposit(daemon_ctx *ctx, int *fds, int nfds) {
  if (nfds != MAX_FDS) {
    dlog("consumer hello with %d fds, expected %d: ignored\n", nfds, MAX_FDS);
    close_fds(fds, nfds);
    return;
  }
  clear_deposit(ctx);
  memcpy(ctx->fds, fds, sizeof(int) * (size_t)nfds);
  ctx->fd_count = nfds;
  dlog("consumer deposited %d fds\n", nfds);
  if (ctx->producer_waiting_fds)
    try_deliver_fds(ctx);
}

static void handle_client_data(daemon_ctx *ctx, struct client *c) {
  struct ctrl_msg hdr;
  struct screen_info payload;
  int fds[MAX_FDS], nfds = 0;

  if (read_msg(c->fd, &hdr, fds, &nfds, &payload) < 0) {
    drop_client(ctx, c);
    return;
  }

  switch (hdr.type) {
  case CTRL_MSG_CONSUMER_HELLO:
    if (c == ctx->consumer) {
      deposit(ctx, fds, nfds);
      return;
    }
    break;

  case CTRL_MSG_SCREEN_INFO:
    if (c == ctx->consumer && hdr.size == sizeof(payload)) {
      /* Always take the newest: the display may have rotated. */
      ctx->screen = payload;
      ctx->has_screen = 1;
      dlog("screen info %ux%u fmt=%u\n", payload.width, payload.height,
           payload.format);
      if (ctx->producer_waiting_screen && ctx->producer) {
        send_screen_info(ctx, ctx->producer->fd);
        ctx->producer_waiting_screen = 0;
      }
    }
    break;

  case CTRL_MSG_PICKUP_FDS:
    if (c == ctx->producer)
      try_deliver_fds(ctx);
    break;

  default:
    break;
  }
  close_fds(fds, nfds);
}

static void handle_new_connection(daemon_ctx *ctx) {
  int fd = accept4(ctx->listen_fd, NULL, NULL, SOCK_CLOEXEC);
  if (fd < 0)
    return;

  struct timeval tv = {.tv_sec = CLIENT_IO_TIMEOUT_S};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct ctrl_msg hdr;
  struct screen_info payload;
  int fds[MAX_FDS], nfds = 0;
  if (read_msg(fd, &hdr, fds, &nfds, &payload) < 0) {
    close(fd);
    return;
  }

  int allowed = 0;
  if (hdr.type == CTRL_MSG_CONSUMER_HELLO)
    allowed = peer_is_root(fd);
  else if (hdr.type == CTRL_MSG_PRODUCER_HELLO)
    allowed = peer_in_container(fd);
  struct client *c = allowed ? calloc(1, sizeof(*c)) : NULL;
  if (!c) {
    if (!allowed)
      dlog("rejected message type %u from an unauthorized peer\n", hdr.type);
    close_fds(fds, nfds);
    close(fd);
    return;
  }
  c->fd = fd;

  if (hdr.type == CTRL_MSG_CONSUMER_HELLO) {
    if (ctx->consumer)
      drop_client(ctx, ctx->consumer);
    ctx->consumer = c;
    dlog("consumer connected\n");
    deposit(ctx, fds, nfds);
  } else {
    if (ctx->producer)
      drop_client(ctx, ctx->producer);
    ctx->producer = c;
    ctx->producer_waiting_screen = 0;
    ctx->producer_waiting_fds = 0;
    close_fds(fds, nfds);
    dlog("producer connected\n");
    if (ctx->has_screen)
      send_screen_info(ctx, fd);
    else
      ctx->producer_waiting_screen = 1;
  }

  struct epoll_event ev = {.events = EPOLLIN | EPOLLHUP | EPOLLERR,
                           .data.ptr = c};
  if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
    drop_client(ctx, c);
}

int daemon_create(daemon_ctx **out, const char *sock_path) {
  daemon_ctx *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    return -1;
  ctx->epoll_fd = -1;
  ctx->listen_fd = -1;
  ctx->running = 1;
  if (strlen(sock_path) >= sizeof(ctx->sock_path)) {
    dlog("socket path too long: %s\n", sock_path);
    free(ctx);
    return -1;
  }
  strcpy(ctx->sock_path, sock_path);

  unlink(sock_path);
  ctx->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (ctx->listen_fd < 0) {
    dlog("socket: %s\n", strerror(errno));
    daemon_destroy(ctx);
    return -1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, sock_path);

  if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(ctx->listen_fd, 4) < 0) {
    dlog("bind %s: %s\n", sock_path, strerror(errno));
    daemon_destroy(ctx);
    return -1;
  }

  ctx->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (ctx->epoll_fd < 0) {
    dlog("epoll_create1: %s\n", strerror(errno));
    daemon_destroy(ctx);
    return -1;
  }
  struct epoll_event ev = {.events = EPOLLIN, .data.ptr = NULL};
  if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->listen_fd, &ev) < 0) {
    dlog("epoll_ctl: %s\n", strerror(errno));
    daemon_destroy(ctx);
    return -1;
  }

  dlog("listening on %s\n", sock_path);
  *out = ctx;
  return 0;
}

void daemon_run(daemon_ctx *ctx) {
  struct epoll_event events[MAX_EVENTS];
  while (ctx->running) {
    int nfds = epoll_wait(ctx->epoll_fd, events, MAX_EVENTS, -1);
    for (int i = 0; i < nfds; i++) {
      if (events[i].data.ptr == NULL) {
        handle_new_connection(ctx);
        continue;
      }
      struct client *c = events[i].data.ptr;
      /* Dropped earlier in this batch: its role is gone and its memory is
       * parked in dead[], so this pointer cannot be a new client. */
      if (c != ctx->consumer && c != ctx->producer)
        continue;
      if (events[i].events & (EPOLLHUP | EPOLLERR))
        drop_client(ctx, c);
      else
        handle_client_data(ctx, c);
    }
    free_dead(ctx);
  }
}

void daemon_stop(daemon_ctx *ctx) {
  if (ctx)
    ctx->running = 0;
}

void daemon_destroy(daemon_ctx *ctx) {
  if (!ctx)
    return;
  clear_deposit(ctx);
  drop_client(ctx, ctx->consumer);
  drop_client(ctx, ctx->producer);
  free_dead(ctx);
  if (ctx->listen_fd >= 0)
    close(ctx->listen_fd);
  if (ctx->epoll_fd >= 0)
    close(ctx->epoll_fd);
  if (ctx->sock_path[0])
    unlink(ctx->sock_path);
  dlog("shutdown\n");
  free(ctx);
}
