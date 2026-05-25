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

#include "droidspace.h"

/*
 * CONCERN(socketd-wire):
 * The Phase 1 protocol records intentionally reuse DS_UUID_LEN and
 * INET_ADDRSTRLEN from the core runtime's public definitions. This keeps the
 * wire format aligned with the current Droidspaces identity/address widths,
 * but it also means this protocol header is no longer width-self-contained.
 */

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
  DS_SOCKETD_OP_LIST_IMAGES = 9,
  DS_SOCKETD_OP_POLL_EVENTS = 10,
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
  DS_SOCKETD_CAP_LIST_IMAGES = 1u << 7,
  DS_SOCKETD_CAP_POLL_EVENTS = 1u << 8,
};

#define DS_SOCKETD_RECORD_NAME_MAX   256
#define DS_SOCKETD_RECORD_PATH_MAX  1024
#define DS_SOCKETD_RECORD_PORTS_MAX   16

/*
 * CONCERN(socketd-wire):
 * This protocol-visible port array width follows the current implementation
 * plan literally. Any bridge serializer using it must keep truncation behavior
 * explicit if a container config contains more entries than fit here.
 */

struct DS_SOCKETD_PACKED ds_socketd_list_containers_req {
  uint8_t include_all; /* 0 = running only, 1 = include stopped */
  uint8_t _pad[3];
};

struct DS_SOCKETD_PACKED ds_socketd_port_record {
  uint16_t host_port_be;
  uint16_t host_port_end_be;      /* 0 if not a range */
  uint16_t container_port_be;
  uint16_t container_port_end_be; /* 0 if not a range */
  uint8_t proto;                  /* 0 = tcp, 1 = udp */
  uint8_t _pad[3];
};

struct DS_SOCKETD_PACKED ds_socketd_container_record {
  char name[DS_SOCKETD_RECORD_NAME_MAX];
  char uuid[DS_UUID_LEN + 1];
  char rootfs_path[DS_SOCKETD_RECORD_PATH_MAX];
  char hostname[DS_SOCKETD_RECORD_NAME_MAX];
  char nat_ip[INET_ADDRSTRLEN]; /* empty string if not NAT mode */
  char custom_init[DS_SOCKETD_RECORD_PATH_MAX]; /* empty = /sbin/init */
  int32_t pid_be; /* host-view PID 1; 0 = stopped */
  uint8_t net_mode; /* 0=host 1=nat 2=none */
  uint8_t port_count; /* entries used in ports[] */
  uint8_t _pad[2];
  struct ds_socketd_port_record ports[DS_SOCKETD_RECORD_PORTS_MAX];
  int64_t started_at_be; /* CLOCK_REALTIME seconds; 0 if unknown */
};

/*
 * CONCERN(socketd-wire):
 * started_at_be is a 64-bit network-order value. Later bridge/client code must
 * use explicit 64-bit byte-order helpers here, not htonl()/ntohl().
 */

struct DS_SOCKETD_PACKED ds_socketd_poll_events_req {
  int64_t since_be; /* return events with time >= this unix timestamp;
                       0 = return all events in the file */
};

#define DS_SOCKETD_EVENT_TYPE_MAX    32
#define DS_SOCKETD_EVENT_ACTION_MAX  32

struct DS_SOCKETD_PACKED ds_socketd_core_event_record {
  int64_t time_be; /* unix seconds */
  int64_t time_nano_be; /* unix nanoseconds */
  char type[DS_SOCKETD_EVENT_TYPE_MAX]; /* e.g. "container" */
  char action[DS_SOCKETD_EVENT_ACTION_MAX]; /* "start","stop","restart","die" */
  char actor_id[DS_UUID_LEN + 1]; /* container UUID */
  char actor_name[DS_SOCKETD_RECORD_NAME_MAX];
};

/*
 * CONCERN(socketd-wire):
 * time_be and time_nano_be are 64-bit network-order fields. Phase 2 bridge
 * code and Phase 3 client code must deserialize them with 64-bit conversion
 * helpers rather than 32-bit ntohl().
 */

struct DS_SOCKETD_PACKED ds_socketd_image_record {
  char name[DS_SOCKETD_RECORD_NAME_MAX]; /* container name used as tag */
  char rootfs_path[DS_SOCKETD_RECORD_PATH_MAX];
  char uuid[DS_UUID_LEN + 1];
  int32_t is_running_be; /* 1 if a container using this rootfs is running */
  int64_t created_at_be; /* 0 if unknown */
  uint8_t _pad[4];
};

struct DS_SOCKETD_PACKED ds_socketd_info_payload {
  uint32_t containers_total_be;
  uint32_t containers_running_be;
  uint32_t containers_stopped_be;
};

/*
 * CONCERN(socketd-wire):
 * created_at_be is likewise a 64-bit network-order field and needs the same
 * explicit conversion treatment in later phases.
 */

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
