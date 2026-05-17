#include "api_server.h"
#include "backend_client.h"

#include "socketd_protocol.h"

#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

using droidspaces::socketd::ApiServer;
using droidspaces::socketd::BackendClient;
using droidspaces::socketd::CapabilitiesResult;
using droidspaces::socketd::TcpListenConfig;
using droidspaces::socketd::parse_tcp_listen_endpoint;

constexpr std::uint32_t kRequiredBackendCapabilities =
    DS_SOCKETD_CAP_PROTOCOL_V1 |
    DS_SOCKETD_CAP_PING |
    DS_SOCKETD_CAP_CAPABILITIES;

void print_usage(const char* argv0) {
  std::cerr
      << "Usage:\n"
      << "  " << argv0 << " --listen-tcp [PORT]\n"
      << "  " << argv0 << " --listen-tcp [ADDR:PORT]\n"
      << "\n"
      << "Examples:\n"
      << "  " << argv0 << " --listen-tcp\n"
      << "  " << argv0 << " --listen-tcp 2375\n"
      << "  " << argv0 << " --listen-tcp 127.0.0.1:2375\n"
      << "  " << argv0 << " --listen-tcp 0.0.0.0:2375\n";
}

bool check_backend(std::string& error) {
  BackendClient backend;

  if (!backend.ping(error)) {
    error = "backend PING failed: " + error;
    return false;
  }

  CapabilitiesResult caps {};
  if (!backend.capabilities(caps, error)) {
    error = "backend CAPABILITIES failed: " + error;
    return false;
  }

  if ((caps.mask & kRequiredBackendCapabilities) !=
      kRequiredBackendCapabilities) {
    error = "backend is missing required base capabilities";
    return false;
  }

  std::cerr << "socketd: backend handshake OK, capabilities mask: 0x"
            << std::hex
            << caps.mask
            << std::dec
            << '\n';

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  /*
   * Avoid process termination if a TCP peer disappears while a response
   * is being written.
   */
  (void)std::signal(SIGPIPE, SIG_IGN);

  bool listen_tcp = false;
  TcpListenConfig tcp_config {};

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    }

    if (arg == "--listen-tcp") {
      if (listen_tcp) {
        std::cerr << "socketd: --listen-tcp specified more than once\n";
        return 2;
      }

      listen_tcp = true;

      if (i + 1 < argc) {
        const std::string next = argv[i + 1];

        if (!next.empty() && next[0] != '-') {
          std::string parse_error;
          if (!parse_tcp_listen_endpoint(next, tcp_config, parse_error)) {
            std::cerr << "socketd: " << parse_error << '\n';
            return 2;
          }

          ++i;
        }
      }

      continue;
    }

    if (arg.rfind("--listen-tcp=", 0) == 0) {
      if (listen_tcp) {
        std::cerr << "socketd: --listen-tcp specified more than once\n";
        return 2;
      }

      listen_tcp = true;

      const std::string value = arg.substr(std::string("--listen-tcp=").size());

      std::string parse_error;
      if (!parse_tcp_listen_endpoint(value, tcp_config, parse_error)) {
        std::cerr << "socketd: " << parse_error << '\n';
        return 2;
      }

      continue;
    }

    std::cerr << "socketd: unknown argument: " << arg << '\n';
    print_usage(argv[0]);
    return 2;
  }

  if (!listen_tcp) {
    std::cerr << "socketd: no listener configured\n";
    print_usage(argv[0]);
    return 2;
  }

  std::string error;
  if (!check_backend(error)) {
    std::cerr << "socketd: " << error << '\n';
    return 1;
  }

  ApiServer server(tcp_config);
  if (!server.run(error)) {
    std::cerr << "socketd: server failed: " << error << '\n';
    return 1;
  }

  return 0;
}
