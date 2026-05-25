#include "snapshot_lists.h"

#include "backend_client.h"

#include <cstddef>
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

void append_image_json(std::string& out,
                       const ImageRecordResult& image) {
  out += "{";

  out += "\"Containers\":";
  out += image.is_running ? "1" : "0";
  out += ",";

  out += "\"Created\":";
  out += std::to_string(image.created_at > 0 ? image.created_at : 0);
  out += ",";

  out += "\"Id\":\"";
  out += json_escape(image.uuid);
  out += "\",";

  out += "\"Labels\":{},";

  out += "\"ParentId\":\"\",";

  out += "\"RepoDigests\":[],";

  out += "\"RepoTags\":[\"";
  out += json_escape(image.name);
  out += ":latest\"],";

  /*
   * CONCERN(socketd-images):
   * This public image summary intentionally follows the current compatibility
   * plan's minimal pseudo-image projection. If a later Docker/Portainer
   * compatibility pass requires additional image-summary fields, extend this
   * socketd JSON seam rather than changing the core-side image record.
   */
  out += "\"Size\":0,";
  out += "\"VirtualSize\":0";

  out += "}";
}

}  // namespace

bool request_image_list_json_from_core(std::string& json_out,
                                       std::string& error) {
  error.clear();

  BackendClient backend;
  std::vector<ImageRecordResult> images;

  if (!backend.list_images(images, error)) {
    return false;
  }

  std::string body;
  body.reserve(256 + images.size() * 256);

  body += "[";

  for (std::size_t i = 0; i < images.size(); ++i) {
    if (i != 0) {
      body += ",";
    }

    append_image_json(body, images[i]);
  }

  body += "]\n";

  json_out = std::move(body);
  return true;
}

bool request_volume_list_json_from_core(std::string& json_out,
                                        std::string& error) {
  error.clear();

  /*
   * Docker-managed named volumes have no direct Droidspaces analogue.
   * Keep the endpoint structurally valid while intentionally exposing none.
   */
  json_out = "{\"Volumes\":[],\"Warnings\":[]}\n";
  return true;
}

bool request_network_list_json_from_core(std::string& json_out,
                                         std::string& error) {
  error.clear();

  /*
   * Fixed public compatibility facade for Droidspaces networking modes.
   * These are socketd-owned Docker-shaped descriptors; no mutable Docker
   * network object is being introduced into the core runtime.
   */
  json_out =
      "["
      "{"
      "\"Name\":\"droidspaces-bridge\","
      "\"Id\":\"droidspaces-bridge\","
      "\"Created\":\"1970-01-01T00:00:00Z\","
      "\"Scope\":\"local\","
      "\"Driver\":\"bridge\","
      "\"EnableIPv6\":false,"
      "\"IPAM\":{\"Driver\":\"default\",\"Options\":{},\"Config\":[]},"
      "\"Internal\":false,"
      "\"Attachable\":false,"
      "\"Ingress\":false,"
      "\"ConfigFrom\":{\"Network\":\"\"},"
      "\"ConfigOnly\":false,"
      "\"Containers\":{},"
      "\"Options\":{},"
      "\"Labels\":{}"
      "},"
      "{"
      "\"Name\":\"host\","
      "\"Id\":\"host\","
      "\"Created\":\"1970-01-01T00:00:00Z\","
      "\"Scope\":\"local\","
      "\"Driver\":\"host\","
      "\"EnableIPv6\":false,"
      "\"IPAM\":{\"Driver\":\"default\",\"Options\":{},\"Config\":[]},"
      "\"Internal\":false,"
      "\"Attachable\":false,"
      "\"Ingress\":false,"
      "\"ConfigFrom\":{\"Network\":\"\"},"
      "\"ConfigOnly\":false,"
      "\"Containers\":{},"
      "\"Options\":{},"
      "\"Labels\":{}"
      "},"
      "{"
      "\"Name\":\"none\","
      "\"Id\":\"none\","
      "\"Created\":\"1970-01-01T00:00:00Z\","
      "\"Scope\":\"local\","
      "\"Driver\":\"null\","
      "\"EnableIPv6\":false,"
      "\"IPAM\":{\"Driver\":\"default\",\"Options\":{},\"Config\":[]},"
      "\"Internal\":false,"
      "\"Attachable\":false,"
      "\"Ingress\":false,"
      "\"ConfigFrom\":{\"Network\":\"\"},"
      "\"ConfigOnly\":false,"
      "\"Containers\":{},"
      "\"Options\":{},"
      "\"Labels\":{}"
      "}"
      "]\n";

  return true;
}

}  // namespace droidspaces::socketd
