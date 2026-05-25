#include "container_list.h"

#include "backend_client.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace droidspaces::socketd {
namespace {

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

const char* container_state(const ContainerRecordResult& record) {
  return record.pid > 0 ? "running" : "exited";
}

const char* container_status(const ContainerRecordResult& record) {
  return record.pid > 0 ? "Up" : "Exited";
}

const char* port_type(std::uint8_t proto) {
  return proto == 1u ? "udp" : "tcp";
}

void append_ports_json(std::string& out,
                       const ContainerRecordResult& record) {
  out += "\"Ports\":[";

  for (std::size_t i = 0; i < record.ports.size(); ++i) {
    if (i != 0) {
      out += ",";
    }

    const ContainerPortResult& port = record.ports[i];

    out += "{";
    out += "\"PrivatePort\":";
    out += std::to_string(port.container_port);
    out += ",";
    out += "\"PublicPort\":";
    out += std::to_string(port.host_port);
    out += ",";
    out += "\"Type\":\"";
    out += port_type(port.proto);
    out += "\"";
    out += "}";
  }

  out += "]";
}

void append_network_settings_json(std::string& out,
                                  const ContainerRecordResult& record) {
  out += "\"NetworkSettings\":{\"Networks\":{";

  /*
   * The Phase 4 public JSON projection follows the current compatibility plan:
   * only NAT-mode containers receive a Docker-style synthetic bridge attachment
   * in /containers/json. Host and none modes remain represented by an empty
   * Networks object here.
   */
  if (record.net_mode == 1u && !record.nat_ip.empty()) {
    out += "\"droidspaces-bridge\":{";
    out += "\"IPAddress\":\"";
    out += json_escape(record.nat_ip);
    out += "\"";
    out += "}";
  }

  out += "}}";
}

void append_container_json(std::string& out,
                           const ContainerRecordResult& record) {
  const std::string command =
      record.custom_init.empty() ? "/sbin/init" : record.custom_init;

  out += "{";

  out += "\"Id\":\"";
  out += json_escape(record.uuid);
  out += "\",";

  out += "\"Names\":[\"/";
  out += json_escape(record.name);
  out += "\"],";

  out += "\"Image\":\"";
  out += json_escape(record.rootfs_path);
  out += "\",";

  out += "\"ImageID\":\"";
  out += json_escape(record.uuid);
  out += "\",";

  out += "\"Command\":\"";
  out += json_escape(command);
  out += "\",";

  out += "\"Created\":";
  out += std::to_string(record.started_at > 0 ? record.started_at : 0);
  out += ",";

  append_ports_json(out, record);
  out += ",";

  out += "\"Labels\":{},";

  out += "\"State\":\"";
  out += container_state(record);
  out += "\",";

  out += "\"Status\":\"";
  out += container_status(record);
  out += "\",";

  append_network_settings_json(out, record);
  out += ",";

  out += "\"Mounts\":[]";

  out += "}";
}

}  // namespace

bool request_container_list_json_from_core(
    const ContainerListRequest& request,
    std::string& json_out,
    std::string& error) {
  error.clear();

  BackendClient backend;
  std::vector<ContainerRecordResult> records;

  if (!backend.list_containers(request.include_all, records, error)) {
    return false;
  }

  std::string body;
  body.reserve(256 + records.size() * 512);

  body += "[";

  for (std::size_t i = 0; i < records.size(); ++i) {
    if (i != 0) {
      body += ",";
    }

    append_container_json(body, records[i]);
  }

  body += "]\n";

  json_out = std::move(body);
  return true;
}

}  // namespace droidspaces::socketd
