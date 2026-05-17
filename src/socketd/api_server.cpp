#include "api_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace droidspaces::socketd {
namespace {

constexpr std::size_t kMaxRequestHeaderBytes = 16 * 1024;

bool send_all(int fd, const void* data, std::size_t len, std::string& error) {
  const auto* p = static_cast<const std::uint8_t*>(data);

  while (len > 0) {
    const ssize_t written = ::send(fd, p, len, 0);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }

      error = "send() failed: ";
      error += std::strerror(errno);
      return false;
    }

    if (written == 0) {
      error = "send() returned 0 unexpectedly";
      return false;
    }

    p += static_cast<std::size_t>(written);
    len -= static_cast<std::size_t>(written);
  }

  return true;
}

bool send_http_response(int fd,
                        int status_code,
                        const char* reason_phrase,
                        const char* content_type,
                        const std::string& body,
                        bool suppress_body,
                        std::string& error) {
  std::string header;
  header.reserve(256);

  header += "HTTP/1.1 ";
  header += std::to_string(status_code);
  header += ' ';
  header += reason_phrase;
  header += "\r\n";

  header += "Content-Type: ";
  header += content_type;
  header += "\r\n";

  header += "Content-Length: ";
  header += std::to_string(body.size());
  header += "\r\n";

  header += "Server: Droidspaces/6 (Container, like Docker)\r\n";
  
  header += "Connection: close\r\n";
  header += "\r\n";

  if (!send_all(fd, header.data(), header.size(), error)) {
    return false;
  }

  if (!suppress_body && !body.empty()) {
    if (!send_all(fd, body.data(), body.size(), error)) {
      return false;
    }
  }

  return true;
}

bool send_bad_request(int fd, bool suppress_body, std::string& error) {
  const std::string body = "{\"message\":\"bad request\"}\n";
  return send_http_response(fd,
                            400,
                            "Bad Request",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_header_too_large(int fd, bool suppress_body, std::string& error) {
  const std::string body = "{\"message\":\"request headers too large\"}\n";
  return send_http_response(fd,
                            431,
                            "Request Header Fields Too Large",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_not_found(int fd, bool suppress_body, std::string& error) {
  const std::string body = "{\"message\":\"not found\"}\n";
  return send_http_response(fd,
                            404,
                            "Not Found",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_ping_ok(int fd, bool suppress_body, std::string& error) {
  const std::string body = "OK";
  return send_http_response(fd,
                            200,
                            "OK",
                            "text/plain; charset=utf-8",
                            body,
                            suppress_body,
                            error);
}

bool is_ascii_digit(char c) {
  return c >= '0' && c <= '9';
}

bool is_versioned_ping_path(const std::string& path) {
  constexpr const char* kSuffix = "/_ping";
  constexpr std::size_t kSuffixLen = 6;

  if (path.size() <= kSuffixLen) {
    return false;
  }

  if (path.compare(path.size() - kSuffixLen, kSuffixLen, kSuffix) != 0) {
    return false;
  }

  const std::string prefix = path.substr(0, path.size() - kSuffixLen);

  /*
   * Accept forms like:
   *
   *   /v1.40/_ping
   *   /v1.51/_ping
   *
   * Prefix must be exactly:
   *
   *   /v<digits>.<digits>
   */
  if (prefix.size() < 5) {
    return false;
  }

  if (prefix[0] != '/' || prefix[1] != 'v') {
    return false;
  }

  std::size_t i = 2;
  const std::size_t major_start = i;

  while (i < prefix.size() && is_ascii_digit(prefix[i])) {
    ++i;
  }

  if (i == major_start) {
    return false;
  }

  if (i >= prefix.size() || prefix[i] != '.') {
    return false;
  }

  ++i;
  const std::size_t minor_start = i;

  while (i < prefix.size() && is_ascii_digit(prefix[i])) {
    ++i;
  }

  if (i == minor_start) {
    return false;
  }

  return i == prefix.size();
}

bool is_ping_target(const std::string& target) {
  const std::size_t query_pos = target.find('?');
  const std::string path =
      query_pos == std::string::npos ? target : target.substr(0, query_pos);

  return path == "/_ping" || is_versioned_ping_path(path);
}

bool parse_port(const std::string& value,
                std::uint16_t& port_out,
                std::string& error) {
  if (value.empty()) {
    error = "TCP port is empty";
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);

  if (errno != 0 || end == value.c_str() || *end != '\0') {
    error = "invalid TCP port: ";
    error += value;
    return false;
  }

  if (parsed == 0 || parsed > 65535UL) {
    error = "TCP port is outside valid range: ";
    error += value;
    return false;
  }

  port_out = static_cast<std::uint16_t>(parsed);
  return true;
}

}  // namespace

bool parse_tcp_listen_endpoint(const std::string& value,
                               TcpListenConfig& out,
                               std::string& error) {
  out = TcpListenConfig {};

  if (value.empty()) {
    return true;
  }

  const std::size_t colon_pos = value.rfind(':');

  if (colon_pos == std::string::npos) {
    return parse_port(value, out.port, error);
  }

  const std::string address = value.substr(0, colon_pos);
  const std::string port = value.substr(colon_pos + 1);

  if (address.empty()) {
    error = "TCP bind address is empty";
    return false;
  }

  out.bind_address = address;
  return parse_port(port, out.port, error);
}

ApiServer::ApiServer(TcpListenConfig config) : config_(std::move(config)) {}

bool ApiServer::create_listener(int& fd_out, std::string& error) const {
  fd_out = -1;

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    error = "socket(AF_INET, SOCK_STREAM) failed: ";
    error += std::strerror(errno);
    return false;
  }

  const int one = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
    error = "setsockopt(SO_REUSEADDR) failed: ";
    error += std::strerror(errno);
    ::close(fd);
    return false;
  }

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config_.port);

  const int pton_result =
      ::inet_pton(AF_INET, config_.bind_address.c_str(), &addr.sin_addr);

  if (pton_result != 1) {
    error = "invalid IPv4 bind address: ";
    error += config_.bind_address;
    ::close(fd);
    return false;
  }

  if (::bind(fd,
             reinterpret_cast<const sockaddr*>(&addr),
             sizeof(addr)) < 0) {
    error = "bind(";
    error += config_.bind_address;
    error += ':';
    error += std::to_string(config_.port);
    error += ") failed: ";
    error += std::strerror(errno);
    ::close(fd);
    return false;
  }

  if (::listen(fd, SOMAXCONN) < 0) {
    error = "listen() failed: ";
    error += std::strerror(errno);
    ::close(fd);
    return false;
  }

  fd_out = fd;
  return true;
}

bool ApiServer::handle_client(int client_fd, std::string& error) const {
  std::string request;
  request.reserve(1024);

  char buffer[4096];

  while (request.find("\r\n\r\n") == std::string::npos) {
    const ssize_t n = ::recv(client_fd, buffer, sizeof(buffer), 0);

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }

      error = "recv() failed: ";
      error += std::strerror(errno);
      return false;
    }

    if (n == 0) {
      error = "client closed connection before sending full HTTP headers";
      return false;
    }

    request.append(buffer, static_cast<std::size_t>(n));

    if (request.size() > kMaxRequestHeaderBytes) {
      std::string response_error;
      (void)send_header_too_large(client_fd, false, response_error);

      error = "request header exceeded configured limit";
      return false;
    }
  }

  const std::size_t line_end = request.find("\r\n");
  if (line_end == std::string::npos) {
    return send_bad_request(client_fd, false, error);
  }

  const std::string request_line = request.substr(0, line_end);

  std::istringstream line_stream(request_line);
  std::string method;
  std::string target;
  std::string version;
  std::string trailing;

  if (!(line_stream >> method >> target >> version) ||
      (line_stream >> trailing)) {
    return send_bad_request(client_fd, false, error);
  }

  if (version.rfind("HTTP/", 0) != 0) {
    return send_bad_request(client_fd, false, error);
  }

  const bool is_head = method == "HEAD";
  const bool is_get = method == "GET";

  if ((is_get || is_head) && is_ping_target(target)) {
    return send_ping_ok(client_fd, is_head, error);
  }

  return send_not_found(client_fd, is_head, error);
}

bool ApiServer::run(std::string& error) {
  int listener_fd = -1;
  if (!create_listener(listener_fd, error)) {
    return false;
  }

  std::cerr << "socketd: listening on http://"
            << config_.bind_address
            << ':'
            << config_.port
            << '\n';

  for (;;) {
    const int client_fd = ::accept(listener_fd, nullptr, nullptr);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }

      error = "accept() failed: ";
      error += std::strerror(errno);
      ::close(listener_fd);
      return false;
    }

    std::string client_error;
    if (!handle_client(client_fd, client_error)) {
      std::cerr << "socketd: client request failed: "
                << client_error
                << '\n';
    }

    ::close(client_fd);
  }
}

}  // namespace droidspaces::socketd
