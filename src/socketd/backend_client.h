#pragma once

#include <cstdint>
#include <string>

namespace droidspaces::socketd {

struct CapabilitiesResult {
  std::uint32_t mask = 0;
};

class BackendClient {
 public:
  BackendClient() = default;

  bool ping(std::string& error) const;
  bool capabilities(CapabilitiesResult& out, std::string& error) const;

 private:
  bool request(std::uint16_t opcode,
               const void* payload,
               std::uint32_t payload_len,
               std::uint16_t& status_out,
               std::string& payload_out,
               std::string& error) const;
};

}  // namespace droidspaces::socketd
