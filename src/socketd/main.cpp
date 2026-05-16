#include "backend_client.h"

#include "socketd_protocol.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

void print_capability(std::uint32_t mask,
                      std::uint32_t bit,
                      const char* name) {
  std::cout << "  " << name << ": "
            << ((mask & bit) ? "yes" : "no") << '\n';
}

}  // namespace

int main() {
  using droidspaces::socketd::BackendClient;
  using droidspaces::socketd::CapabilitiesResult;

  BackendClient backend;
  std::string error;

  if (!backend.ping(error)) {
    std::cerr << "socketd: backend PING failed: " << error << '\n';
    return 1;
  }

  std::cout << "socketd: backend PING OK\n";

  CapabilitiesResult caps {};
  if (!backend.capabilities(caps, error)) {
    std::cerr << "socketd: backend CAPABILITIES failed: " << error << '\n';
    return 1;
  }

  std::cout << "socketd: backend capabilities mask: 0x"
            << std::hex << caps.mask << std::dec << '\n';

  print_capability(caps.mask, DS_SOCKETD_CAP_PROTOCOL_V1, "protocol-v1");
  print_capability(caps.mask, DS_SOCKETD_CAP_PING, "ping");
  print_capability(caps.mask, DS_SOCKETD_CAP_CAPABILITIES, "capabilities");
  print_capability(caps.mask, DS_SOCKETD_CAP_INFO, "info");
  print_capability(caps.mask, DS_SOCKETD_CAP_LIST_CONTAINERS, "list-containers");
  print_capability(caps.mask, DS_SOCKETD_CAP_INSPECT_CONTAINER, "inspect-container");
  print_capability(caps.mask, DS_SOCKETD_CAP_LIFECYCLE, "lifecycle");

  return 0;
}
