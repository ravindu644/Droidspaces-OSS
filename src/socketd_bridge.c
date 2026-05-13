/*
 * Droidspaces v6 - private backend bridge for droidspaces-socketd
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include "socketd_bridge.h"
#include "socketd_protocol.h"

/*
 * The public Docker/Podman-compatible socket belongs to the external C++
 * droidspaces-socketd daemon.  This bridge is deliberately narrower: it is a
 * local, private, privileged control path that will eventually expose the
 * Droidspaces-native operations needed by that compatibility daemon.
 */

static int socketd_read_exact(int fd, void *buf, size_t len) {
  uint8_t *p = (uint8_t *)buf;
  while (len > 0) {
    ssize_t r = read(fd, p, len);
    if (r == 0)
      return -1;
    if (r < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    p += (size_t)r;
    len -= (size_t)r;
  }
  return 0;
}

static socklen_t socketd_backend_addr(struct sockaddr_un *addr) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;

  size_t name_len = strlen(DS_SOCKETD_BACKEND_SOCK_NAME);
  if (name_len >= sizeof(addr->sun_path))
    name_len = sizeof(addr->sun_path) - 1;

  memcpy(addr->sun_path + 1, DS_SOCKETD_BACKEND_SOCK_NAME, name_len);
  return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);
}

static int socketd_send_response(int fd, enum ds_socketd_status status,
                                 const void *payload, uint32_t payload_len) {
  struct ds_socketd_response_header hdr;
  memset(&hdr, 0, sizeof(hdr));
  hdr.magic_be = htonl(DS_SOCKETD_PROTO_MAGIC);
  hdr.version_be = htons(DS_SOCKETD_PROTO_VERSION);
  hdr.status_be = htons((uint16_t)status);
  hdr.payload_len_be = htonl(payload_len);

  if (write_all(fd, &hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr))
    return -1;

  if (payload_len > 0 && payload != NULL) {
    if (write_all(fd, payload, payload_len) != (ssize_t)payload_len)
      return -1;
  }
  return 0;
}

static int socketd_peer_authorized(int fd) {
#ifdef SO_PEERCRED
  struct ucred cred;
  socklen_t cred_len = sizeof(cred);
  memset(&cred, 0, sizeof(cred));

  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0)
    return 0;
  if (cred_len != sizeof(cred))
    return 0;

  /*
   * The first socketd implementation is expected to run as root beside the
   * Droidspaces daemon.  Same-EUID access keeps local developer/test launches
   * usable on desktop Linux without opening the bridge to arbitrary users.
   */
  return cred.uid == 0 || cred.uid == geteuid();
#else
  (void)fd;
  return 0;
#endif
}

static int socketd_discard_payload(int fd, uint32_t len) {
  char buf[4096];
  uint32_t remaining = len;
  while (remaining > 0) {
    size_t chunk = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
    if (socketd_read_exact(fd, buf, chunk) < 0)
      return -1;
    remaining -= (uint32_t)chunk;
  }
  return 0;
}

static void socketd_handle_conn(int conn) {
  struct ds_socketd_request_header req;
  memset(&req, 0, sizeof(req));

  if (!socketd_peer_authorized(conn)) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_FORBIDDEN, NULL, 0);
    return;
  }

  if (socketd_read_exact(conn, &req, sizeof(req)) < 0) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
    return;
  }

  uint32_t magic = ntohl(req.magic_be);
  uint16_t version = ntohs(req.version_be);
  uint16_t opcode = ntohs(req.opcode_be);
  uint32_t payload_len = ntohl(req.payload_len_be);

  if (magic != DS_SOCKETD_PROTO_MAGIC || version != DS_SOCKETD_PROTO_VERSION) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
    return;
  }

  if (payload_len > DS_SOCKETD_MAX_PAYLOAD) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
    return;
  }

  /*
   * The currently implemented opcodes do not consume payloads, but draining
   * a well-sized payload keeps the framing strict and future-proofs callers.
   */
  if (payload_len > 0 && socketd_discard_payload(conn, payload_len) < 0) {
    socketd_send_response(conn, DS_SOCKETD_STATUS_BAD_REQUEST, NULL, 0);
    return;
  }

  switch ((enum ds_socketd_opcode)opcode) {
  case DS_SOCKETD_OP_PING: {
    static const char pong[] = "PONG";
    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, pong,
                          (uint32_t)(sizeof(pong) - 1));
    return;
  }

  case DS_SOCKETD_OP_CAPABILITIES: {
    uint32_t caps_be = htonl(DS_SOCKETD_CAP_PROTOCOL_V1 |
                             DS_SOCKETD_CAP_PING |
                             DS_SOCKETD_CAP_CAPABILITIES);
    socketd_send_response(conn, DS_SOCKETD_STATUS_OK, &caps_be,
                          (uint32_t)sizeof(caps_be));
    return;
  }

  case DS_SOCKETD_OP_INFO:
  case DS_SOCKETD_OP_LIST_CONTAINERS:
  case DS_SOCKETD_OP_INSPECT_CONTAINER:
  case DS_SOCKETD_OP_START_CONTAINER:
  case DS_SOCKETD_OP_STOP_CONTAINER:
  case DS_SOCKETD_OP_RESTART_CONTAINER:
    socketd_send_response(conn, DS_SOCKETD_STATUS_UNSUPPORTED, NULL, 0);
    return;

  default:
    socketd_send_response(conn, DS_SOCKETD_STATUS_UNSUPPORTED, NULL, 0);
    return;
  }
}

static int socketd_bridge_loop(void) {
  struct sockaddr_un addr;
  int server = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server < 0)
    return -1;

  fcntl(server, F_SETFD, FD_CLOEXEC);

  socklen_t addr_len = socketd_backend_addr(&addr);
  if (bind(server, (struct sockaddr *)&addr, addr_len) < 0) {
    close(server);
    return -1;
  }

  if (listen(server, SOMAXCONN) < 0) {
    close(server);
    return -1;
  }

  ds_log("droidspaces-socketd backend bridge listening on @%s",
         DS_SOCKETD_BACKEND_SOCK_NAME);

  for (;;) {
    int conn = accept(server, NULL, NULL);
    if (conn < 0) {
      if (errno == EINTR)
        continue;
      ds_warn("socketd backend accept failed: %s", strerror(errno));
      continue;
    }

    fcntl(conn, F_SETFD, FD_CLOEXEC);
    socketd_handle_conn(conn);
    close(conn);
  }

  return 0;
}

int ds_socketd_bridge_start(void) {
  pid_t child = fork();
  if (child < 0)
    return -1;

  if (child > 0) {
    ds_log("droidspaces-socketd backend bridge process started (PID %d)", child);
    return 0;
  }

  prctl(PR_SET_NAME, "[ds-socketd]", 0, 0, 0);
  signal(SIGPIPE, SIG_IGN);

#ifdef PR_SET_PDEATHSIG
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  if (getppid() == 1)
    _exit(0);
#endif

  int rc = socketd_bridge_loop();
  ds_error("droidspaces-socketd backend bridge exited: %s", strerror(errno));
  _exit(rc == 0 ? 0 : 1);
}
