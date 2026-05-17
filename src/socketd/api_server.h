#pragma once

#include <cstdint>
#include <string>

namespace droidspaces::socketd {

struct TcpListenConfig {
  std::string bind_address = "0.0.0.0";
  std::uint16_t port = 2375;
};

/*
 * Parses:
 *
 *   ""                  -> 0.0.0.0:2375
 *   "2375"              -> 0.0.0.0:2375
 *   "127.0.0.1:2375"    -> 127.0.0.1:2375
 *   "0.0.0.0:2375"      -> 0.0.0.0:2375
 *
 * This first revision intentionally accepts IPv4 only.
 */
bool parse_tcp_listen_endpoint(const std::string& value,
                               TcpListenConfig& out,
                               std::string& error);

class ApiServer {
 public:
  explicit ApiServer(TcpListenConfig config);

  bool run(std::string& error);

 private:
  bool create_listener(int& fd_out, std::string& error) const;
  bool handle_client(int client_fd, std::string& error) const;

  TcpListenConfig config_;
};

}  // namespace droidspaces::socketd
