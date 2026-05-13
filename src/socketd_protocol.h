/*
 * Droidspaces v6 - private socketd backend wire protocol
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This header is deliberately C/C++ compatible.  The existing Droidspaces
 * daemon consumes it from C; the future droidspaces-socketd daemon will use it
 * from C++ without pulling C++ types into the native runtime.
 */

#ifndef DROIDSPACES_SOCKETD_PROTOCOL_H
#define DROIDSPACES_SOCKETD_PROTOCOL_H

#include <stdint.h>

#if defined(__GNUC__)
#define DS_SOCKETD_PACKED __attribute__((packed))
#else
#define DS_SOCKETD_PACKED
#endif

#define DS_SOCKETD_BACKEND_SOCK_NAME "droidspaces-socketd-backend"

/* ASCII "DSAP" in network byte order after htonl(). */
#define DS_SOCKETD_PROTO_MAGIC 0x44534150u
#define DS_SOCKETD_PROTO_VERSION 1u
#define DS_SOCKETD_MAX_PAYLOAD (1024u * 1024u)

enum ds_socketd_opcode {
  DS_SOCKETD_OP_PING = 1,
  DS_SOCKETD_OP_CAPABILITIES = 2,
  DS_SOCKETD_OP_INFO = 3,
  DS_SOCKETD_OP_LIST_CONTAINERS = 4,
  DS_SOCKETD_OP_INSPECT_CONTAINER = 5,
  DS_SOCKETD_OP_START_CONTAINER = 6,
  DS_SOCKETD_OP_STOP_CONTAINER = 7,
  DS_SOCKETD_OP_RESTART_CONTAINER = 8,
};

enum ds_socketd_status {
  DS_SOCKETD_STATUS_OK = 0,
  DS_SOCKETD_STATUS_BAD_REQUEST = 1,
  DS_SOCKETD_STATUS_UNSUPPORTED = 2,
  DS_SOCKETD_STATUS_NOT_FOUND = 3,
  DS_SOCKETD_STATUS_INTERNAL_ERROR = 4,
  DS_SOCKETD_STATUS_FORBIDDEN = 5,
};

enum ds_socketd_capability {
  DS_SOCKETD_CAP_PROTOCOL_V1 = 1u << 0,
  DS_SOCKETD_CAP_PING = 1u << 1,
  DS_SOCKETD_CAP_CAPABILITIES = 1u << 2,
  DS_SOCKETD_CAP_INFO = 1u << 3,
  DS_SOCKETD_CAP_LIST_CONTAINERS = 1u << 4,
  DS_SOCKETD_CAP_INSPECT_CONTAINER = 1u << 5,
  DS_SOCKETD_CAP_LIFECYCLE = 1u << 6,
};

/*
 * Request frame:
 *   magic_be       DS_SOCKETD_PROTO_MAGIC via htonl()
 *   version_be     DS_SOCKETD_PROTO_VERSION via htons()
 *   opcode_be      enum ds_socketd_opcode via htons()
 *   payload_len_be number of payload bytes via htonl()
 */
struct DS_SOCKETD_PACKED ds_socketd_request_header {
  uint32_t magic_be;
  uint16_t version_be;
  uint16_t opcode_be;
  uint32_t payload_len_be;
};

/*
 * Response frame:
 *   magic_be       DS_SOCKETD_PROTO_MAGIC via htonl()
 *   version_be     DS_SOCKETD_PROTO_VERSION via htons()
 *   status_be      enum ds_socketd_status via htons()
 *   payload_len_be number of payload bytes via htonl()
 */
struct DS_SOCKETD_PACKED ds_socketd_response_header {
  uint32_t magic_be;
  uint16_t version_be;
  uint16_t status_be;
  uint32_t payload_len_be;
};

#endif /* DROIDSPACES_SOCKETD_PROTOCOL_H */
