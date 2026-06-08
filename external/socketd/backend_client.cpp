#include "backend_client.h"

#include "socketd_protocol.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace droidspaces::socketd {
namespace {

bool write_exact(int fd, const void* buf, std::size_t len, std::string& error) {
  const auto* p = static_cast<const std::uint8_t*>(buf);

  while (len > 0) {
    const ssize_t written = ::write(fd, p, len);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }

      error = "write() failed: ";
      error += std::strerror(errno);
      return false;
    }

    if (written == 0) {
      error = "write() returned 0 unexpectedly";
      return false;
    }

    p += static_cast<std::size_t>(written);
    len -= static_cast<std::size_t>(written);
  }

  return true;
}

bool read_exact(int fd, void* buf, std::size_t len, std::string& error) {
  auto* p = static_cast<std::uint8_t*>(buf);

  while (len > 0) {
    const ssize_t received = ::read(fd, p, len);
    if (received < 0) {
      if (errno == EINTR) {
        continue;
      }

      error = "read() failed: ";
      error += std::strerror(errno);
      return false;
    }

    if (received == 0) {
      error = "peer closed connection before full response arrived";
      return false;
    }

    p += static_cast<std::size_t>(received);
    len -= static_cast<std::size_t>(received);
  }

  return true;
}

int connect_backend(std::string& error) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    error = "socket(AF_UNIX) failed: ";
    error += std::strerror(errno);
    return -1;
  }

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;

  const std::size_t name_len = std::strlen(DS_SOCKETD_BACKEND_SOCK_NAME);
  if (name_len >= sizeof(addr.sun_path)) {
    error = "backend abstract socket name is too long";
    ::close(fd);
    return -1;
  }

  /*
   * Linux abstract AF_UNIX address:
   *   sun_path[0] = '\0'
   *   sun_path[1..] = socket name
   */
  std::memcpy(addr.sun_path + 1, DS_SOCKETD_BACKEND_SOCK_NAME, name_len);

  const socklen_t addr_len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + 1 + name_len);

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), addr_len) < 0) {
    error = "connect(@";
    error += DS_SOCKETD_BACKEND_SOCK_NAME;
    error += ") failed: ";
    error += std::strerror(errno);
    ::close(fd);
    return -1;
  }

  return fd;
}

std::uint64_t ntoh64(std::uint64_t value) {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  return value;
#else
  return (static_cast<std::uint64_t>(
              ntohl(static_cast<std::uint32_t>(value & 0xffffffffULL)))
          << 32) |
         static_cast<std::uint64_t>(
             ntohl(static_cast<std::uint32_t>(value >> 32)));
#endif
}

std::uint64_t hton64(std::uint64_t value) {
  return ntoh64(value);
}

std::string decode_fixed_string(const char* data, std::size_t size) {
  if (!data || size == 0) {
    return {};
  }

  const void* nul = std::memchr(data, '\0', size);
  const std::size_t len =
      nul ? static_cast<const char*>(nul) - data : size;

  /*
   * CONCERN(socketd-wire):
   * Backend wire strings are fixed-width char arrays rather than
   * length-prefixed blobs. Decode defensively and tolerate a field that fills
   * its entire slot without an in-bounds NUL terminator.
   */
  return std::string(data, len);
}

bool expect_ok_status(std::uint16_t status,
                      const char* operation,
                      std::string& error) {
  if (status == DS_SOCKETD_STATUS_OK) {
    return true;
  }

  error = operation;
  error += " returned backend status ";
  error += std::to_string(status);
  return false;
}

}  // namespace

bool BackendClient::request(std::uint16_t opcode,
                            const void* payload,
                            std::uint32_t payload_len,
                            std::uint16_t& status_out,
                            std::string& payload_out,
                            std::string& error) const {
  if (payload_len > DS_SOCKETD_MAX_PAYLOAD) {
    error = "request payload exceeds DS_SOCKETD_MAX_PAYLOAD";
    return false;
  }

  /*
   * The current C bridge handles exactly one request per accepted connection,
   * then closes it. Mirror that contract here: one RPC == one connection.
   */
  const int fd = connect_backend(error);
  if (fd < 0) {
    return false;
  }

  ds_socketd_request_header req {};
  req.magic_be = htonl(DS_SOCKETD_PROTO_MAGIC);
  req.version_be = htons(DS_SOCKETD_PROTO_VERSION);
  req.opcode_be = htons(opcode);
  req.payload_len_be = htonl(payload_len);

  if (!write_exact(fd, &req, sizeof(req), error)) {
    ::close(fd);
    return false;
  }

  if (payload_len > 0 && payload != nullptr) {
    if (!write_exact(fd, payload, payload_len, error)) {
      ::close(fd);
      return false;
    }
  }

  ds_socketd_response_header resp {};
  if (!read_exact(fd, &resp, sizeof(resp), error)) {
    ::close(fd);
    return false;
  }

  const std::uint32_t magic = ntohl(resp.magic_be);
  const std::uint16_t version = ntohs(resp.version_be);
  const std::uint16_t status = ntohs(resp.status_be);
  const std::uint32_t response_payload_len = ntohl(resp.payload_len_be);

  if (magic != DS_SOCKETD_PROTO_MAGIC) {
    error = "backend response used invalid protocol magic";
    ::close(fd);
    return false;
  }

  if (version != DS_SOCKETD_PROTO_VERSION) {
    error = "backend response used unsupported protocol version";
    ::close(fd);
    return false;
  }

  if (response_payload_len > DS_SOCKETD_MAX_PAYLOAD) {
    error = "backend response payload exceeds DS_SOCKETD_MAX_PAYLOAD";
    ::close(fd);
    return false;
  }

  payload_out.clear();
  payload_out.resize(response_payload_len);

  if (response_payload_len > 0) {
    if (!read_exact(fd, payload_out.data(), response_payload_len, error)) {
      ::close(fd);
      return false;
    }
  }

  ::close(fd);

  status_out = status;
  return true;
}

bool BackendClient::ping(std::string& error) const {
  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(DS_SOCKETD_OP_PING, nullptr, 0, status, payload, error)) {
    return false;
  }

  if (status != DS_SOCKETD_STATUS_OK) {
    error = "PING returned backend status ";
    error += std::to_string(status);
    return false;
  }

  if (payload != "PONG") {
    error = "PING returned unexpected payload: ";
    error += payload;
    return false;
  }

  return true;
}

bool BackendClient::capabilities(CapabilitiesResult& out,
                                 std::string& error) const {
  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(DS_SOCKETD_OP_CAPABILITIES, nullptr, 0,
               status, payload, error)) {
    return false;
  }

  if (status != DS_SOCKETD_STATUS_OK) {
    error = "CAPABILITIES returned backend status ";
    error += std::to_string(status);
    return false;
  }

  if (payload.size() != sizeof(std::uint32_t)) {
    error = "CAPABILITIES returned payload of unexpected size";
    return false;
  }

  std::uint32_t mask_be = 0;
  std::memcpy(&mask_be, payload.data(), sizeof(mask_be));
  out.mask = ntohl(mask_be);

  return true;
}

bool BackendClient::list_containers(
    bool include_all,
    std::vector<ContainerRecordResult>& out,
    std::string& error) const {
  ds_socketd_list_containers_req req {};
  req.include_all = include_all ? 1u : 0u;

  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(DS_SOCKETD_OP_LIST_CONTAINERS,
               &req,
               static_cast<std::uint32_t>(sizeof(req)),
               status,
               payload,
               error)) {
    return false;
  }

  if (!expect_ok_status(status, "LIST_CONTAINERS", error)) {
    return false;
  }

  if (payload.size() % sizeof(ds_socketd_container_record) != 0) {
    error = "LIST_CONTAINERS returned payload of invalid size";
    return false;
  }

  out.clear();

  const std::size_t count =
      payload.size() / sizeof(ds_socketd_container_record);
  out.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    ds_socketd_container_record wire {};
    std::memcpy(&wire,
                payload.data() + i * sizeof(wire),
                sizeof(wire));

    ContainerRecordResult result;
    result.name =
        decode_fixed_string(wire.name, sizeof(wire.name));
    result.uuid =
        decode_fixed_string(wire.uuid, sizeof(wire.uuid));
    result.rootfs_path =
        decode_fixed_string(wire.rootfs_path, sizeof(wire.rootfs_path));
    result.hostname =
        decode_fixed_string(wire.hostname, sizeof(wire.hostname));
    result.nat_ip =
        decode_fixed_string(wire.nat_ip, sizeof(wire.nat_ip));
    result.custom_init =
        decode_fixed_string(wire.custom_init, sizeof(wire.custom_init));

    result.pid = static_cast<std::int32_t>(
        ntohl(static_cast<std::uint32_t>(wire.pid_be)));

    result.net_mode = wire.net_mode;

    result.started_at = static_cast<std::int64_t>(
        ntoh64(static_cast<std::uint64_t>(wire.started_at_be)));

    std::size_t port_count = wire.port_count;
    if (port_count > DS_SOCKETD_RECORD_PORTS_MAX) {
      port_count = DS_SOCKETD_RECORD_PORTS_MAX;
    }

    result.ports.reserve(port_count);

    for (std::size_t j = 0; j < port_count; ++j) {
      const ds_socketd_port_record& port_wire = wire.ports[j];

      ContainerPortResult port;
      port.host_port = ntohs(port_wire.host_port_be);
      port.host_port_end = ntohs(port_wire.host_port_end_be);
      port.container_port = ntohs(port_wire.container_port_be);
      port.container_port_end =
          ntohs(port_wire.container_port_end_be);
      port.proto = port_wire.proto;

      result.ports.push_back(std::move(port));
    }

    out.push_back(std::move(result));
  }

  return true;
}


bool BackendClient::inspect_container(const std::string& ref,
                                      ContainerInspectResult& out,
                                      bool& not_found,
                                      std::string& error) const {
  not_found = false;

  if (ref.empty() || ref.size() >= DS_SOCKETD_RECORD_NAME_MAX) {
    error = "container reference is empty or too long";
    return false;
  }

  ds_socketd_inspect_container_req req {};
  std::memcpy(req.target, ref.c_str(), ref.size());

  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(DS_SOCKETD_OP_INSPECT_CONTAINER,
               &req,
               static_cast<std::uint32_t>(sizeof(req)),
               status,
               payload,
               error)) {
    return false;
  }

  if (status == DS_SOCKETD_STATUS_NOT_FOUND) {
    not_found = true;
    error = "container not found";
    return false;
  }

  if (!expect_ok_status(status, "INSPECT_CONTAINER", error)) {
    return false;
  }

  if (payload.size() != sizeof(ds_socketd_inspect_container_record_v1)) {
    error = "INSPECT_CONTAINER returned payload of unexpected size";
    return false;
  }

  ds_socketd_inspect_container_record_v1 wire {};
  std::memcpy(&wire, payload.data(), sizeof(wire));

  const std::uint16_t record_version = ntohs(wire.record_version_be);
  const std::uint32_t record_size = ntohl(wire.record_size_be);
  if (record_version != 1u || record_size != sizeof(wire)) {
    error = "INSPECT_CONTAINER returned unsupported inspect record version";
    return false;
  }

  ContainerInspectResult result;
  result.name = decode_fixed_string(wire.name, sizeof(wire.name));
  result.uuid = decode_fixed_string(wire.uuid, sizeof(wire.uuid));
  result.rootfs_path =
      decode_fixed_string(wire.rootfs_path, sizeof(wire.rootfs_path));
  result.image_ref =
      decode_fixed_string(wire.image_ref, sizeof(wire.image_ref));
  result.hostname = decode_fixed_string(wire.hostname, sizeof(wire.hostname));
  result.nat_ip = decode_fixed_string(wire.nat_ip, sizeof(wire.nat_ip));
  result.custom_init =
      decode_fixed_string(wire.custom_init, sizeof(wire.custom_init));
  result.dns_servers =
      decode_fixed_string(wire.dns_servers, sizeof(wire.dns_servers));

  result.pid = static_cast<std::int32_t>(
      ntohl(static_cast<std::uint32_t>(wire.pid_be)));
  result.started_at = static_cast<std::int64_t>(
      ntoh64(static_cast<std::uint64_t>(wire.started_at_be)));
  result.memory_limit = static_cast<std::int64_t>(
      ntoh64(static_cast<std::uint64_t>(wire.memory_limit_be)));
  result.cpu_quota = static_cast<std::int64_t>(
      ntoh64(static_cast<std::uint64_t>(wire.cpu_quota_be)));
  result.cpu_period = static_cast<std::int64_t>(
      ntoh64(static_cast<std::uint64_t>(wire.cpu_period_be)));
  result.pids_limit = static_cast<std::int64_t>(
      ntoh64(static_cast<std::uint64_t>(wire.pids_limit_be)));
  result.privileged_mask = static_cast<std::int32_t>(
      ntohl(static_cast<std::uint32_t>(wire.privileged_mask_be)));

  result.net_mode = wire.net_mode;
  result.foreground = wire.foreground != 0;
  result.volatile_mode = wire.volatile_mode != 0;
  result.force_cgroupv1 = wire.force_cgroupv1 != 0;
  result.disable_ipv6 = wire.disable_ipv6 != 0;
  result.android_storage = wire.android_storage != 0;
  result.selinux_permissive = wire.selinux_permissive != 0;
  result.hw_access = wire.hw_access != 0;
  result.gpu_mode = wire.gpu_mode != 0;
  result.termux_x11 = wire.termux_x11 != 0;
  result.block_nested_ns = wire.block_nested_ns != 0;
  result.is_img_mount = wire.is_img_mount != 0;

  std::size_t env_count = ntohs(wire.env_count_be);
  if (env_count > DS_SOCKETD_INSPECT_ENV_MAX) {
    env_count = DS_SOCKETD_INSPECT_ENV_MAX;
  }
  result.env_total_count = ntohs(wire.env_total_count_be);
  result.env.reserve(env_count);
  for (std::size_t i = 0; i < env_count; ++i) {
    InspectEnvResult env;
    env.key = decode_fixed_string(wire.env[i].key, sizeof(wire.env[i].key));
    env.value =
        decode_fixed_string(wire.env[i].value, sizeof(wire.env[i].value));
    result.env.push_back(std::move(env));
  }

  std::size_t bind_count = ntohs(wire.bind_count_be);
  if (bind_count > DS_SOCKETD_INSPECT_BINDS_MAX) {
    bind_count = DS_SOCKETD_INSPECT_BINDS_MAX;
  }
  result.bind_total_count = ntohs(wire.bind_total_count_be);
  result.binds.reserve(bind_count);
  for (std::size_t i = 0; i < bind_count; ++i) {
    InspectBindResult bind;
    bind.source = decode_fixed_string(wire.binds[i].source,
                                      sizeof(wire.binds[i].source));
    bind.destination = decode_fixed_string(wire.binds[i].destination,
                                           sizeof(wire.binds[i].destination));
    bind.read_only = wire.binds[i].read_only != 0;
    result.binds.push_back(std::move(bind));
  }

  std::size_t port_count = ntohs(wire.port_count_be);
  if (port_count > DS_SOCKETD_RECORD_PORTS_MAX) {
    port_count = DS_SOCKETD_RECORD_PORTS_MAX;
  }
  result.port_total_count = ntohs(wire.port_total_count_be);
  result.ports.reserve(port_count);
  for (std::size_t i = 0; i < port_count; ++i) {
    const ds_socketd_port_record& port_wire = wire.ports[i];

    ContainerPortResult port;
    port.host_port = ntohs(port_wire.host_port_be);
    port.host_port_end = ntohs(port_wire.host_port_end_be);
    port.container_port = ntohs(port_wire.container_port_be);
    port.container_port_end = ntohs(port_wire.container_port_end_be);
    port.proto = port_wire.proto;
    result.ports.push_back(std::move(port));
  }

  out = std::move(result);
  return true;
}


bool BackendClient::info(InfoResult& out, std::string& error) const {
  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(DS_SOCKETD_OP_INFO, nullptr, 0,
               status, payload, error)) {
    return false;
  }

  if (!expect_ok_status(status, "INFO", error)) {
    return false;
  }

  if (payload.size() != sizeof(ds_socketd_info_payload)) {
    error = "INFO returned payload of unexpected size";
    return false;
  }

  ds_socketd_info_payload wire {};
  std::memcpy(&wire, payload.data(), sizeof(wire));

  out.containers_total = ntohl(wire.containers_total_be);
  out.containers_running = ntohl(wire.containers_running_be);
  out.containers_stopped = ntohl(wire.containers_stopped_be);

  return true;
}

bool BackendClient::list_images(
    std::vector<ImageRecordResult>& out,
    std::string& error) const {
  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(DS_SOCKETD_OP_LIST_IMAGES, nullptr, 0,
               status, payload, error)) {
    return false;
  }

  if (!expect_ok_status(status, "LIST_IMAGES", error)) {
    return false;
  }

  if (payload.size() % sizeof(ds_socketd_image_record) != 0) {
    error = "LIST_IMAGES returned payload of invalid size";
    return false;
  }

  out.clear();

  const std::size_t count =
      payload.size() / sizeof(ds_socketd_image_record);
  out.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    ds_socketd_image_record wire {};
    std::memcpy(&wire,
                payload.data() + i * sizeof(wire),
                sizeof(wire));

    ImageRecordResult result;
    result.name =
        decode_fixed_string(wire.name, sizeof(wire.name));
    result.rootfs_path =
        decode_fixed_string(wire.rootfs_path, sizeof(wire.rootfs_path));
    result.uuid =
        decode_fixed_string(wire.uuid, sizeof(wire.uuid));

    result.is_running =
        ntohl(static_cast<std::uint32_t>(wire.is_running_be)) != 0;

    result.created_at = static_cast<std::int64_t>(
        ntoh64(static_cast<std::uint64_t>(wire.created_at_be)));

    out.push_back(std::move(result));
  }

  return true;
}



bool BackendClient::lifecycle_request(std::uint16_t opcode,
                                      const std::string& ref,
                                      int timeout_seconds,
                                      LifecycleResult& out,
                                      std::string& error) const {
  out = LifecycleResult {};

  if (ref.empty() || ref.size() >= DS_SOCKETD_RECORD_NAME_MAX) {
    error = "container reference is empty or too long";
    return false;
  }

  if (timeout_seconds < -1) {
    error = "container lifecycle timeout is invalid";
    return false;
  }

  ds_socketd_lifecycle_req req {};
  std::memcpy(req.target, ref.c_str(), ref.size());
  req.timeout_seconds_be = static_cast<std::int32_t>(
      htonl(static_cast<std::uint32_t>(timeout_seconds)));

  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(opcode,
               &req,
               static_cast<std::uint32_t>(sizeof(req)),
               status,
               payload,
               error)) {
    return false;
  }

  if (status == DS_SOCKETD_STATUS_OK) {
    return true;
  }

  switch (status) {
    case DS_SOCKETD_STATUS_NOT_FOUND:
      out.not_found = true;
      error = "container not found";
      return false;
    case DS_SOCKETD_STATUS_ALREADY_RUNNING:
      out.already_running = true;
      error = "container already running";
      return false;
    case DS_SOCKETD_STATUS_ALREADY_STOPPED:
      out.already_stopped = true;
      error = "container already stopped";
      return false;
    default:
      return expect_ok_status(status, "LIFECYCLE", error);
  }
}

bool BackendClient::start_container(const std::string& ref,
                                    LifecycleResult& out,
                                    std::string& error) const {
  return lifecycle_request(DS_SOCKETD_OP_START_CONTAINER, ref, -1, out, error);
}

bool BackendClient::stop_container(const std::string& ref,
                                   int timeout_seconds,
                                   LifecycleResult& out,
                                   std::string& error) const {
  return lifecycle_request(DS_SOCKETD_OP_STOP_CONTAINER,
                           ref,
                           timeout_seconds,
                           out,
                           error);
}

bool BackendClient::restart_container(const std::string& ref,
                                      int timeout_seconds,
                                      LifecycleResult& out,
                                      std::string& error) const {
  return lifecycle_request(DS_SOCKETD_OP_RESTART_CONTAINER,
                           ref,
                           timeout_seconds,
                           out,
                           error);
}

bool BackendClient::poll_events(
    std::int64_t since,
    std::vector<CoreEventResult>& out,
    std::string& error) const {
  if (since < 0) {
    error = "POLL_EVENTS does not accept a negative 'since' value";
    return false;
  }

  ds_socketd_poll_events_req req {};
  req.since_be = static_cast<std::int64_t>(
      hton64(static_cast<std::uint64_t>(since)));

  std::uint16_t status = DS_SOCKETD_STATUS_INTERNAL_ERROR;
  std::string payload;

  if (!request(DS_SOCKETD_OP_POLL_EVENTS,
               &req,
               static_cast<std::uint32_t>(sizeof(req)),
               status,
               payload,
               error)) {
    return false;
  }

  if (!expect_ok_status(status, "POLL_EVENTS", error)) {
    return false;
  }

  if (payload.size() % sizeof(ds_socketd_core_event_record) != 0) {
    error = "POLL_EVENTS returned payload of invalid size";
    return false;
  }

  out.clear();

  const std::size_t count =
      payload.size() / sizeof(ds_socketd_core_event_record);
  out.reserve(count);

  for (std::size_t i = 0; i < count; ++i) {
    ds_socketd_core_event_record wire {};
    std::memcpy(&wire,
                payload.data() + i * sizeof(wire),
                sizeof(wire));

    CoreEventResult result;
    result.time = static_cast<std::int64_t>(
        ntoh64(static_cast<std::uint64_t>(wire.time_be)));
    result.time_nano = static_cast<std::int64_t>(
        ntoh64(static_cast<std::uint64_t>(wire.time_nano_be)));

    result.type =
        decode_fixed_string(wire.type, sizeof(wire.type));
    result.action =
        decode_fixed_string(wire.action, sizeof(wire.action));
    result.actor_id =
        decode_fixed_string(wire.actor_id, sizeof(wire.actor_id));
    result.actor_name =
        decode_fixed_string(wire.actor_name, sizeof(wire.actor_name));

    out.push_back(std::move(result));
  }

  return true;
}

}  // namespace droidspaces::socketd
