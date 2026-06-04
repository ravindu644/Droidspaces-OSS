#include "api_server.h"
#include "backend_client.h"
#include "container_list.h"
#include "container_inspect.h"
#include "snapshot_lists.h"
#include "event_log.h"
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
#include <vector>

#include <limits.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace droidspaces::socketd {
namespace {

constexpr std::size_t kMaxRequestHeaderBytes = 16 * 1024;
constexpr const char* kSocketApiVersion = "1.41";
constexpr const char* kSocketMinApiVersion = "1.40";
constexpr const char* kSocketOsType = "linux";

constexpr std::uint64_t kMaxStaticAssetBytes = 32ULL * 1024ULL * 1024ULL;
constexpr const char* kDefaultWebIndex = "index.html";

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


bool send_empty_http_response(int fd,
                              int status_code,
                              const char* reason_phrase,
                              std::string& error) {
  std::string header;
  header.reserve(192);

  header += "HTTP/1.1 ";
  header += std::to_string(status_code);
  header += ' ';
  header += reason_phrase;
  header += "\r\n";
  header += "Content-Length: 0\r\n";
  header += "Server: Droidspaces/6 (Container, like Docker)\r\n";
  header += "Api-Version: ";
  header += kSocketApiVersion;
  header += "\r\n";
  header += "Ostype: ";
  header += kSocketOsType;
  header += "\r\n";
  header += "Connection: close\r\n";
  header += "\r\n";

  return send_all(fd, header.data(), header.size(), error);
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


bool send_internal_server_error(int fd,
                                const std::string& message,
                                bool suppress_body,
                                std::string& error) {
  std::string body = "{\"message\":\"";
  body += json_escape(message.empty() ? "internal server error" : message);
  body += "\"}\n";

  return send_http_response(fd,
                            500,
                            "Internal Server Error",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_no_content(int fd, std::string& error) {
  return send_empty_http_response(fd, 204, "No Content", error);
}

bool send_not_modified(int fd, std::string& error) {
  return send_empty_http_response(fd, 304, "Not Modified", error);
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

int hex_digit_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }

  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }

  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }

  return -1;
}

bool percent_decode_url_path(const std::string& input,
                             std::string& output) {
  output.clear();
  output.reserve(input.size());

  for (std::size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];

    if (c != '%') {
      if (c == '\0') {
        return false;
      }

      output += c;
      continue;
    }

    if (i + 2 >= input.size()) {
      return false;
    }

    const int hi = hex_digit_value(input[i + 1]);
    const int lo = hex_digit_value(input[i + 2]);
    if (hi < 0 || lo < 0) {
      return false;
    }

    const char decoded = static_cast<char>((hi << 4) | lo);
    if (decoded == '\0') {
      return false;
    }

    output += decoded;
    i += 2;
  }

  return true;
}

std::string dirname_of(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  if (slash == std::string::npos) {
    return ".";
  }

  if (slash == 0) {
    return "/";
  }

  return path.substr(0, slash);
}

std::string socketd_binary_dir() {
  char buffer[PATH_MAX] {};

  const ssize_t len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len <= 0) {
    return ".";
  }

  buffer[len] = '\0';
  return dirname_of(buffer);
}

const std::string& web_root_dir() {
  static const std::string root = socketd_binary_dir() + "/../www/html";
  return root;
}

std::string lowercase_ascii(std::string value) {
  for (char& c : value) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }

  return value;
}

std::string content_type_for_path(const std::string& path) {
  const std::size_t slash = path.rfind('/');
  const std::size_t dot = path.rfind('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return "application/octet-stream";
  }

  const std::string ext = lowercase_ascii(path.substr(dot + 1));

  if (ext == "html" || ext == "htm") {
    return "text/html; charset=utf-8";
  }

  if (ext == "css") {
    return "text/css; charset=utf-8";
  }

  if (ext == "js" || ext == "mjs") {
    return "application/javascript; charset=utf-8";
  }

  if (ext == "json" || ext == "map") {
    return "application/json; charset=utf-8";
  }

  if (ext == "svg") {
    return "image/svg+xml";
  }

  if (ext == "png") {
    return "image/png";
  }

  if (ext == "jpg" || ext == "jpeg") {
    return "image/jpeg";
  }

  if (ext == "gif") {
    return "image/gif";
  }

  if (ext == "webp") {
    return "image/webp";
  }

  if (ext == "ico") {
    return "image/x-icon";
  }

  if (ext == "txt") {
    return "text/plain; charset=utf-8";
  }

  if (ext == "wasm") {
    return "application/wasm";
  }

  return "application/octet-stream";
}

bool build_static_asset_path(const std::string& target,
                             std::string& file_path,
                             std::string& error) {
  file_path.clear();

  const std::size_t query_pos = target.find('?');
  const std::string raw_path =
      query_pos == std::string::npos ? target : target.substr(0, query_pos);

  if (raw_path.empty() || raw_path[0] != '/') {
    error = "static asset request target is not an origin-form path";
    return false;
  }

  std::string decoded_path;
  if (!percent_decode_url_path(raw_path, decoded_path)) {
    error = "static asset path contains invalid percent encoding";
    return false;
  }

  if (decoded_path.empty() || decoded_path[0] != '/' ||
      decoded_path.find('\\') != std::string::npos) {
    error = "static asset path is invalid";
    return false;
  }

  std::vector<std::string> segments;
  std::size_t pos = 1;
  while (pos <= decoded_path.size()) {
    const std::size_t slash = decoded_path.find('/', pos);
    const std::size_t end = slash == std::string::npos ? decoded_path.size()
                                                        : slash;
    const std::string segment = decoded_path.substr(pos, end - pos);

    if (!segment.empty() && segment != ".") {
      if (segment == "..") {
        error = "static asset path attempts to leave document root";
        return false;
      }

      segments.push_back(segment);
    }

    if (slash == std::string::npos) {
      break;
    }

    pos = slash + 1;
  }

  if (segments.empty() || decoded_path.back() == '/') {
    segments.push_back(kDefaultWebIndex);
  }

  std::string relative_path;
  for (const std::string& segment : segments) {
    if (!relative_path.empty()) {
      relative_path += '/';
    }

    relative_path += segment;
  }

  file_path = web_root_dir() + "/" + relative_path;
  return true;
}

bool read_regular_file(const std::string& file_path,
                       std::string& body,
                       bool& not_found,
                       std::string& error) {
  not_found = false;
  body.clear();

  struct stat st {};
  if (::stat(file_path.c_str(), &st) != 0) {
    if (errno == ENOENT || errno == ENOTDIR || errno == EACCES) {
      not_found = true;
      return false;
    }

    error = "stat(" + file_path + ") failed: ";
    error += std::strerror(errno);
    return false;
  }

  if (!S_ISREG(st.st_mode)) {
    not_found = true;
    return false;
  }

  if (st.st_size < 0 ||
      static_cast<std::uint64_t>(st.st_size) > kMaxStaticAssetBytes) {
    error = "static asset is too large: " + file_path;
    return false;
  }

  std::ifstream file(file_path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    if (errno == ENOENT || errno == ENOTDIR || errno == EACCES) {
      not_found = true;
      return false;
    }

    error = "open(" + file_path + ") failed: ";
    error += std::strerror(errno);
    return false;
  }

  body.resize(static_cast<std::size_t>(st.st_size));
  if (!body.empty()) {
    file.read(&body[0], static_cast<std::streamsize>(body.size()));
    if (!file) {
      error = "read(" + file_path + ") failed";
      return false;
    }
  }

  return true;
}

bool send_static_asset_ok(int fd,
                          const std::string& target,
                          bool suppress_body,
                          std::string& error) {
  std::string file_path;
  std::string path_error;
  if (!build_static_asset_path(target, file_path, path_error)) {
    return send_bad_request(fd, suppress_body, error);
  }

  std::string body;
  bool not_found = false;
  if (!read_regular_file(file_path, body, not_found, error)) {
    if (not_found) {
      return send_not_found(fd, suppress_body, error);
    }

    return send_internal_server_error(fd, error, suppress_body, error);
  }

  const std::string content_type = content_type_for_path(file_path);
  return send_http_response(fd,
                            200,
                            "OK",
                            content_type.c_str(),
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


bool strip_api_version_prefix(const std::string& path,
                              std::string& unversioned_path) {
  if (path.rfind("/v", 0) != 0) {
    unversioned_path = path;
    return true;
  }

  std::size_t i = 2;
  const std::size_t major_start = i;

  while (i < path.size() && is_ascii_digit(path[i])) {
    ++i;
  }

  if (i == major_start || i >= path.size() || path[i] != '.') {
    unversioned_path = path;
    return true;
  }

  ++i;
  const std::size_t minor_start = i;

  while (i < path.size() && is_ascii_digit(path[i])) {
    ++i;
  }

  if (i == minor_start || i >= path.size() || path[i] != '/') {
    unversioned_path = path;
    return true;
  }

  unversioned_path = path.substr(i);
  return true;
}

bool parse_container_ref_with_suffix(const std::string& target,
                                     const char* suffix,
                                     std::string& ref_out) {
  const std::size_t query_pos = target.find('?');
  const std::string path =
      query_pos == std::string::npos ? target : target.substr(0, query_pos);

  std::string unversioned_path;
  if (!strip_api_version_prefix(path, unversioned_path)) {
    return false;
  }

  constexpr const char* kPrefix = "/containers/";
  const std::size_t prefix_len = std::strlen(kPrefix);
  const std::size_t suffix_len = std::strlen(suffix);

  if (unversioned_path.size() <= prefix_len + suffix_len ||
      unversioned_path.compare(0, prefix_len, kPrefix) != 0 ||
      unversioned_path.compare(unversioned_path.size() - suffix_len,
                               suffix_len,
                               suffix) != 0) {
    return false;
  }

  ref_out = unversioned_path.substr(
      prefix_len,
      unversioned_path.size() - prefix_len - suffix_len);

  return !ref_out.empty() && ref_out.find('/') == std::string::npos;
}

bool parse_container_inspect_ref(const std::string& target,
                                 std::string& ref_out) {
  return parse_container_ref_with_suffix(target, "/json", ref_out);
}

bool parse_container_action_ref(const std::string& target,
                                const char* action_suffix,
                                std::string& ref_out) {
  return parse_container_ref_with_suffix(target, action_suffix, ref_out);
}

bool is_truthy_query_value(const std::string& value) {
  return value.empty() ||
         value == "1" ||
         value == "true";
}

ContainerListRequest parse_container_list_request(
    const std::string& target) {
  ContainerListRequest request {};

  const std::size_t query_pos = target.find('?');
  if (query_pos == std::string::npos ||
      query_pos + 1 >= target.size()) {
    return request;
  }

  /*
   * This is intentionally a very small query parser for the bring-up phase.
   * It extracts only the public API semantic that socketd currently cares
   * about: ?all=1. Unknown query parameters are ignored.
   */
  std::size_t pos = query_pos + 1;

  while (pos <= target.size()) {
    const std::size_t amp = target.find('&', pos);
    const std::size_t end =
        amp == std::string::npos ? target.size() : amp;

    const std::string item = target.substr(pos, end - pos);
    const std::size_t eq = item.find('=');

    const std::string key =
        eq == std::string::npos ? item : item.substr(0, eq);

    const std::string value =
        eq == std::string::npos ? "" : item.substr(eq + 1);

    if (key == "all") {
      request.include_all = is_truthy_query_value(value);
    }

    if (amp == std::string::npos) {
      break;
    }

    pos = amp + 1;
  }

  return request;
}

EventsRequest parse_events_request(const std::string& target) {
  EventsRequest request {};

  const std::size_t query_pos = target.find('?');
  if (query_pos == std::string::npos ||
      query_pos + 1 >= target.size()) {
    return request;
  }

  /*
   * Small, deliberate parser for only the fields observed from Portainer's
   * event-log request. Unknown parameters are ignored for now.
   */
  std::size_t pos = query_pos + 1;

  while (pos <= target.size()) {
    const std::size_t amp = target.find('&', pos);
    const std::size_t end =
        amp == std::string::npos ? target.size() : amp;

    const std::string item = target.substr(pos, end - pos);
    const std::size_t eq = item.find('=');

    const std::string key =
        eq == std::string::npos ? item : item.substr(0, eq);

    const std::string value =
        eq == std::string::npos ? "" : item.substr(eq + 1);

    if (key == "since") {
      request.since = value;
    } else if (key == "until") {
      request.until = value;
    }

    if (amp == std::string::npos) {
      break;
    }

    pos = amp + 1;
  }

  return request;
}


struct ContainerLifecycleRequest {
  int timeout_seconds = -1;
};

bool parse_nonnegative_int(const std::string& value,
                           int& out,
                           std::string& error) {
  if (value.empty()) {
    error = "empty integer value";
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (errno != 0 || end == value.c_str() || *end != '\0' || parsed < 0 ||
      parsed > std::numeric_limits<int>::max()) {
    error = "invalid non-negative integer: ";
    error += value;
    return false;
  }

  out = static_cast<int>(parsed);
  return true;
}

bool parse_container_lifecycle_request(const std::string& target,
                                       ContainerLifecycleRequest& request,
                                       std::string& error) {
  request = ContainerLifecycleRequest {};

  const std::size_t query_pos = target.find('?');
  if (query_pos == std::string::npos || query_pos + 1 >= target.size()) {
    return true;
  }

  std::size_t pos = query_pos + 1;
  while (pos <= target.size()) {
    const std::size_t amp = target.find('&', pos);
    const std::size_t end =
        amp == std::string::npos ? target.size() : amp;

    const std::string item = target.substr(pos, end - pos);
    const std::size_t eq = item.find('=');
    const std::string key =
        eq == std::string::npos ? item : item.substr(0, eq);
    const std::string value =
        eq == std::string::npos ? "" : item.substr(eq + 1);

    if (key == "t") {
      if (!parse_nonnegative_int(value, request.timeout_seconds, error)) {
        error = "invalid lifecycle timeout query parameter: " + value;
        return false;
      }
    }

    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }

  return true;
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

bool build_info_json(std::string& body, std::string& error) {
  BackendClient backend;
  InfoResult info;

  if (!backend.info(info, error)) {
    return false;
  }

  /*
   * /info remains primarily a socketd-local Docker-shaped compatibility
   * document: host facts such as kernel, architecture, CPU count, and memory
   * are gathered in this process. Backend INFO contributes the live
   * Droidspaces inventory counters.
   */
  const std::string arch = socketd_arch_name();
  const std::string kernel_version = socketd_kernel_version();
  const std::string hostname = socketd_hostname();
  const std::string system_time = socketd_system_time_utc();
  const unsigned int ncpu = socketd_ncpu();
  const std::uint64_t mem_total = socketd_mem_total_bytes();

  body.clear();
  body.reserve(1400);

  body += "{";

  body += "\"ID\":\"\",";

  body += "\"Containers\":";
  body += std::to_string(info.containers_total);
  body += ",";

  body += "\"ContainersRunning\":";
  body += std::to_string(info.containers_running);
  body += ",";

  body += "\"ContainersPaused\":0,";

  body += "\"ContainersStopped\":";
  body += std::to_string(info.containers_stopped);
  body += ",";

  /*
   * CONCERN(socketd-info):
   * The current INFO backend payload carries container inventory counters only.
   * Preserve the existing Images=0 field until the plan explicitly extends the
   * INFO wire payload or chooses a socketd-side count source for pseudo-images.
   */
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
  return true;
}

bool send_info_ok(int fd, bool suppress_body, std::string& error) {
  std::string body;

  if (!build_info_json(body, error)) {
    return false;
  }

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_container_list_ok(int fd,
                            const std::string& target,
                            bool suppress_body,
                            std::string& error) {
  const ContainerListRequest request =
      parse_container_list_request(target);

  std::string body;
  if (!request_container_list_json_from_core(request, body, error)) {
    return false;
  }

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}


bool send_container_inspect_ok(int fd,
                               const std::string& ref,
                               bool suppress_body,
                               std::string& error) {
  std::string body;
  bool not_found = false;

  if (!request_container_inspect_json_from_core(ref, body, not_found, error)) {
    if (not_found) {
      return send_not_found(fd, suppress_body, error);
    }

    return false;
  }

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_container_start_ok(int fd,
                             const std::string& ref,
                             std::string& error) {
  BackendClient backend;
  LifecycleResult result;
  std::string backend_error;

  if (backend.start_container(ref, result, backend_error)) {
    return send_no_content(fd, error);
  }

  if (result.not_found) {
    return send_not_found(fd, false, error);
  }

  if (result.already_running) {
    return send_not_modified(fd, error);
  }

  return send_internal_server_error(fd, backend_error, false, error);
}

bool send_container_stop_ok(int fd,
                            const std::string& target,
                            const std::string& ref,
                            std::string& error) {
  ContainerLifecycleRequest request;
  std::string parse_error;
  if (!parse_container_lifecycle_request(target, request, parse_error)) {
    return send_bad_request(fd, false, error);
  }

  BackendClient backend;
  LifecycleResult result;
  std::string backend_error;

  if (backend.stop_container(ref, request.timeout_seconds, result,
                             backend_error)) {
    return send_no_content(fd, error);
  }

  if (result.not_found) {
    return send_not_found(fd, false, error);
  }

  if (result.already_stopped) {
    return send_not_modified(fd, error);
  }

  return send_internal_server_error(fd, backend_error, false, error);
}

bool send_container_restart_ok(int fd,
                               const std::string& target,
                               const std::string& ref,
                               std::string& error) {
  ContainerLifecycleRequest request;
  std::string parse_error;
  if (!parse_container_lifecycle_request(target, request, parse_error)) {
    return send_bad_request(fd, false, error);
  }

  BackendClient backend;
  LifecycleResult result;
  std::string backend_error;

  if (backend.restart_container(ref, request.timeout_seconds, result,
                                backend_error)) {
    return send_no_content(fd, error);
  }

  if (result.not_found) {
    return send_not_found(fd, false, error);
  }

  return send_internal_server_error(fd, backend_error, false, error);
}

bool send_image_list_ok(int fd,
                        bool suppress_body,
                        std::string& error) {
  std::string body;
  if (!request_image_list_json_from_core(body, error)) {
    return false;
  }

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_volume_list_ok(int fd,
                         bool suppress_body,
                         std::string& error) {
  std::string body;
  if (!request_volume_list_json_from_core(body, error)) {
    return false;
  }

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_network_list_ok(int fd,
                          bool suppress_body,
                          std::string& error) {
  std::string body;
  if (!request_network_list_json_from_core(body, error)) {
    return false;
  }

  return send_http_response(fd,
                            200,
                            "OK",
                            "application/json",
                            body,
                            suppress_body,
                            error);
}

bool send_events_ok(int fd,
                    const std::string& target,
                    bool suppress_body,
                    std::string& error) {
  const EventsRequest request = parse_events_request(target);

  std::string body;
  if (!request_event_log_stream_from_core(request, body, error)) {
    return false;
  }

  /*
   * API v1.40-compatible behavior:
   * Moby used application/json for event streams at this API level.
   * An empty body is intentional and accepted by Portainer's event-log parser.
   */
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
  const bool is_post = method == "POST";

  if ((is_get || is_head) && is_api_target(target, "/_ping")) {
    return send_ping_ok(client_fd, is_head, error);
  }

  if (is_get && is_api_target(target, "/version")) {
    return send_version_ok(client_fd, false, error);
  }

  if (is_get && is_api_target(target, "/info")) {
    return send_info_ok(client_fd, false, error);
  }


  if (is_get && is_api_target(target, "/containers/json")) {
    return send_container_list_ok(client_fd, target, false, error);
  }

  std::string inspect_ref;
  if (is_get && parse_container_inspect_ref(target, inspect_ref)) {
    return send_container_inspect_ok(client_fd, inspect_ref, false, error);
  }

  std::string lifecycle_ref;
  if (is_post && parse_container_action_ref(target, "/start", lifecycle_ref)) {
    return send_container_start_ok(client_fd, lifecycle_ref, error);
  }

  if (is_post && parse_container_action_ref(target, "/stop", lifecycle_ref)) {
    return send_container_stop_ok(client_fd, target, lifecycle_ref, error);
  }

  if (is_post && parse_container_action_ref(target, "/restart", lifecycle_ref)) {
    return send_container_restart_ok(client_fd, target, lifecycle_ref, error);
  }

  if (is_get && is_api_target(target, "/images/json")) {
    return send_image_list_ok(client_fd, false, error);
  }

  if (is_get && is_api_target(target, "/volumes")) {
    return send_volume_list_ok(client_fd, false, error);
  }

  if (is_get && is_api_target(target, "/networks")) {
    return send_network_list_ok(client_fd, false, error);
  }

  if (is_get && is_api_target(target, "/events")) {
    return send_events_ok(client_fd, target, false, error);
  }

  if (is_get || is_head) {
    return send_static_asset_ok(client_fd, target, is_head, error);
  }

  return send_not_found(client_fd, false, error);
}

bool ApiServer::run(std::string& error) {
  int listener_fd = -1;
  if (!create_listener(listener_fd, error)) {
    return false;
  }
// To tty
  std::cerr << "socketd: listening on http://"
            << config_.bind_address
            << ':'
            << config_.port
            << '\n';
// To API
  const std::string listen_target =
      "tcp://" + config_.bind_address + ":" + std::to_string(config_.port);

  const SocketdEventAttributes attrs[] = {
      {"name", "droidspaces-socketd"},
      {"component", "socketd"},
      {"listen", listen_target},
  };

  record_socketd_event("daemon",
                       "start",
                       "droidspaces-socketd",
                       attrs,
                       sizeof(attrs) / sizeof(attrs[0]));

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
