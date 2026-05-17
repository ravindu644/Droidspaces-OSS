#include "api_server.h"
#include "../droidspace.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <ctime>
#include <fstream>
#include <limits>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace droidspaces::socketd {
namespace {

constexpr std::size_t kMaxRequestHeaderBytes = 16 * 1024;
constexpr const char* kSocketApiVersion = "1.40";
constexpr const char* kSocketMinApiVersion = "1.40";
constexpr const char* kSocketOsType = "linux";

std::string socketd_arch_name() {
#if defined(__x86_64__)
  return "amd64";
#elif defined(__i386__)
  return "386";
#elif defined(__aarch64__)
  return "arm64";
#elif defined(__arm__)
  return "arm";
#elif defined(__riscv) && (__riscv_xlen == 64)
  return "riscv64";
#else
  return "unknown";
#endif
}

std::string socketd_kernel_version() {
  struct utsname uts {};
  if (::uname(&uts) != 0) {
    return {};
  }

  return uts.release;
}

std::uint64_t socketd_mem_total_bytes() {
  std::ifstream meminfo("/proc/meminfo");
  if (!meminfo.is_open()) {
    return 0;
  }

  std::string key;
  std::uint64_t value_kib = 0;
  std::string unit;

  while (meminfo >> key >> value_kib >> unit) {
    if (key == "MemTotal:") {
      /*
       * /proc/meminfo reports MemTotal in KiB.
       */
      return value_kib * 1024ULL;
    }
  }

  return 0;
}

unsigned int socketd_ncpu() {
  const long value = ::sysconf(_SC_NPROCESSORS_ONLN);
  if (value <= 0) {
    return 0;
  }

  if (static_cast<unsigned long>(value) >
      std::numeric_limits<unsigned int>::max()) {
    return std::numeric_limits<unsigned int>::max();
  }

  return static_cast<unsigned int>(value);
}

std::string socketd_hostname() {
  char hostname[256] {};
  if (::gethostname(hostname, sizeof(hostname) - 1) != 0) {
    return "droidspaces";
  }

  hostname[sizeof(hostname) - 1] = '\0';

  if (hostname[0] == '\0') {
    return "droidspaces";
  }

  return hostname;
}

std::string socketd_system_time_utc() {
  std::time_t now = std::time(nullptr);
  if (now == static_cast<std::time_t>(-1)) {
    return {};
  }

  std::tm tm {};
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__ANDROID__) || defined(__linux__)
  if (::gmtime_r(&now, &tm) == nullptr) {
    return {};
  }
#else
  const std::tm* tmp = std::gmtime(&now);
  if (tmp == nullptr) {
    return {};
  }
  tm = *tmp;
#endif

  char buffer[64] {};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
    return {};
  }

  return buffer;
}

std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size());

  constexpr char kHex[] = "0123456789abcdef";

  for (unsigned char ch : input) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20) {
          out += "\\u00";
          out += kHex[(ch >> 4) & 0x0f];
          out += kHex[ch & 0x0f];
        } else {
          out += static_cast<char>(ch);
        }
        break;
    }
  }

  return out;
}

void debug_log_request_headers(const std::string& request) {
  const std::size_t header_end = request.find("\r\n\r\n");

  const std::size_t visible_len =
      header_end == std::string::npos
          ? request.size()
          : header_end + 4;

  std::cerr << "socketd: received HTTP request headers\n";
  std::cerr << "----- BEGIN REQUEST -----\n";
  std::cerr.write(request.data(), static_cast<std::streamsize>(visible_len));

  /*
   * HTTP headers already end with CRLF CRLF, but ensure the terminal output
   * does not visually run into the separator if a malformed request arrives.
   */
  if (visible_len == 0 ||
      request[visible_len - 1] != '\n') {
    std::cerr << '\n';
  }

  std::cerr << "----- END REQUEST -----\n";
}

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
  
  header += "Api-Version: ";
  header += kSocketApiVersion;
  header += "\r\n";

  header += "Ostype: ";
  header += kSocketOsType;
  header += "\r\n";
  
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

bool is_versioned_api_path(const std::string& path,
                           const char* endpoint_path) {
  const std::size_t endpoint_len = std::strlen(endpoint_path);

  if (path.size() <= endpoint_len) {
    return false;
  }

  if (path.compare(path.size() - endpoint_len,
                   endpoint_len,
                   endpoint_path) != 0) {
    return false;
  }

  const std::string prefix =
      path.substr(0, path.size() - endpoint_len);

  /*
   * Accept:
   *
   *   /v1.40/_ping
   *   /v1.40/version
   *
   * Prefix must be:
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

bool is_api_target(const std::string& target,
                   const char* endpoint_path) {
  const std::size_t query_pos = target.find('?');
  const std::string path =
      query_pos == std::string::npos ? target : target.substr(0, query_pos);

  return path == endpoint_path ||
         is_versioned_api_path(path, endpoint_path);
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

std::string build_version_json() {
  const std::string arch = socketd_arch_name();
  const std::string kernel_version = socketd_kernel_version();

  std::string body;
  body.reserve(512);

  body += "{";

  body += "\"Platform\":{\"Name\":\"";
  body += json_escape(DS_PROJECT_NAME);
  body += "\"},";

  body += "\"Components\":[{";
  body += "\"Name\":\"Engine\",";
  body += "\"Version\":\"";
  body += json_escape(DS_VERSION);
  body += "\",";
  body += "\"Details\":{}";
  body += "}],";

  body += "\"Version\":\"";
  body += json_escape(DS_VERSION);
  body += "\",";

  body += "\"ApiVersion\":\"";
  body += kSocketApiVersion;
  body += "\",";

  body += "\"MinAPIVersion\":\"";
  body += kSocketMinApiVersion;
  body += "\",";

  body += "\"Os\":\"";
  body += kSocketOsType;
  body += "\",";

  body += "\"Arch\":\"";
  body += json_escape(arch);
  body += "\"";

  if (!kernel_version.empty()) {
    body += ",\"KernelVersion\":\"";
    body += json_escape(kernel_version);
    body += "\"";
  }

  body += "}\n";
  return body;
}

bool send_version_ok(int fd, bool suppress_body, std::string& error) {
  const std::string body = build_version_json();

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

std::string build_info_json() {
  /*
   * TODO(socketd):
   * This /info response is intentionally synthesized locally so that early
   * Portainer integration can proceed and reveal the next compatibility
   * requirements. Replace or enrich this with DS_SOCKETD_OP_INFO once the
   * privileged backend bridge has a stable information payload.
   */
  const std::string arch = socketd_arch_name();
  const std::string kernel_version = socketd_kernel_version();
  const std::string hostname = socketd_hostname();
  const std::string system_time = socketd_system_time_utc();
  const unsigned int ncpu = socketd_ncpu();
  const std::uint64_t mem_total = socketd_mem_total_bytes();

  std::string body;
  body.reserve(1400);

  body += "{";

  /*
   * Container and image counters are placeholders until INFO is backed by
   * the privileged daemon.
   */
  body += "\"ID\":\"\",";
  body += "\"Containers\":0,";
  body += "\"ContainersRunning\":0,";
  body += "\"ContainersPaused\":0,";
  body += "\"ContainersStopped\":0,";
  body += "\"Images\":0,";

  body += "\"Driver\":\"droidspaces\",";
  body += "\"DriverStatus\":[],";
  body += "\"Plugins\":{";
  body += "\"Volume\":[],";
  body += "\"Network\":[],";
  body += "\"Authorization\":[],";
  body += "\"Log\":[]";
  body += "},";

  body += "\"MemoryLimit\":false,";
  body += "\"SwapLimit\":false,";
  body += "\"CpuCfsPeriod\":false,";
  body += "\"CpuCfsQuota\":false,";
  body += "\"CPUShares\":false,";
  body += "\"CPUSet\":false,";
  body += "\"PidsLimit\":false,";
  body += "\"IPv4Forwarding\":false,";
  body += "\"Debug\":false,";
  body += "\"NFd\":0,";
  body += "\"OomKillDisable\":false,";
  body += "\"NGoroutines\":0,";

  body += "\"SystemTime\":\"";
  body += json_escape(system_time);
  body += "\",";

  body += "\"LoggingDriver\":\"\",";
  body += "\"CgroupDriver\":\"\",";
  body += "\"NEventsListener\":0,";

  body += "\"KernelVersion\":\"";
  body += json_escape(kernel_version);
  body += "\",";

  body += "\"OperatingSystem\":\"Droidspaces\",";
  body += "\"OSVersion\":\"\",";
  body += "\"OSType\":\"linux\",";

  body += "\"Architecture\":\"";
  body += json_escape(arch);
  body += "\",";

  body += "\"IndexServerAddress\":\"\",";
  body += "\"RegistryConfig\":null,";

  body += "\"NCPU\":";
  body += std::to_string(ncpu);
  body += ",";

  body += "\"MemTotal\":";
  body += std::to_string(mem_total);
  body += ",";

  body += "\"GenericResources\":[],";
  body += "\"DockerRootDir\":\"\",";
  body += "\"HttpProxy\":\"\",";
  body += "\"HttpsProxy\":\"\",";
  body += "\"NoProxy\":\"\",";

  body += "\"Name\":\"";
  body += json_escape(hostname);
  body += "\",";

  body += "\"Labels\":[],";
  body += "\"ExperimentalBuild\":false,";

  body += "\"ServerVersion\":\"";
  body += json_escape(DS_VERSION);
  body += "\",";

  body += "\"Runtimes\":{},";
  body += "\"DefaultRuntime\":\"\",";
  body += "\"Swarm\":{\"NodeID\":\"\"},";
  body += "\"LiveRestoreEnabled\":false,";
  body += "\"Isolation\":\"\",";
  body += "\"InitBinary\":\"\",";
  body += "\"ContainerdCommit\":{\"ID\":\"\"},";
  body += "\"RuncCommit\":{\"ID\":\"\"},";
  body += "\"InitCommit\":{\"ID\":\"\"},";
  body += "\"SecurityOptions\":[],";
  body += "\"Warnings\":[]";

  body += "}\n";
  return body;
}

bool send_info_ok(int fd, bool suppress_body, std::string& error) {
  const std::string body = build_info_json();

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
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
// DEBUG FEATURE: perhaps remove later; do NOT expect this to stay.
  debug_log_request_headers(request);

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

  if ((is_get || is_head) && is_api_target(target, "/_ping")) {
    return send_ping_ok(client_fd, is_head, error);
  }
  
  if (is_get && is_api_target(target, "/version")) {
    return send_version_ok(client_fd, false, error);
  }
  
  if (is_get && is_api_target(target, "/info")) {
    return send_info_ok(client_fd, false, error);
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
