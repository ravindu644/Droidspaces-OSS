#include "backend_client.h"

#include "socketd_protocol.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

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

}  // namespace droidspaces::socketd
