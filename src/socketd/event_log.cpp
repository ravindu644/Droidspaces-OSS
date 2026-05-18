#include "event_log.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <cerrno>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace droidspaces::socketd {
namespace {

constexpr std::size_t kMaxSocketdEvents = 128;

struct EventAttribute {
  std::string key;
  std::string value;
};

struct SocketdEvent {
  std::string type;
  std::string action;
  std::string actor_id;
  std::vector<EventAttribute> attributes;
  std::int64_t time = 0;
  std::int64_t time_nano = 0;
};

std::deque<SocketdEvent> g_socketd_events;

std::int64_t unix_time_seconds_now() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  return std::chrono::duration_cast<std::chrono::seconds>(
             now.time_since_epoch())
      .count();
}

std::int64_t unix_time_nanos_now() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
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

bool parse_optional_epoch_seconds(const std::string& value,
                                  std::int64_t& out,
                                  std::string& error) {
  out = 0;

  if (value.empty()) {
    return true;
  }

  char* end = nullptr;
  errno = 0;
  const long long parsed = std::strtoll(value.c_str(), &end, 10);

  if (errno != 0 || end == value.c_str() || *end != '\0') {
    error = "invalid event timestamp: ";
    error += value;
    return false;
  }

  if (parsed < 0) {
    error = "negative event timestamp is unsupported: ";
    error += value;
    return false;
  }

  out = static_cast<std::int64_t>(parsed);
  return true;
}

bool event_in_window(const SocketdEvent& event,
                     std::int64_t since,
                     std::int64_t until) {
  if (since > 0 && event.time < since) {
    return false;
  }

  if (until > 0 && event.time > until) {
    return false;
  }

  return true;
}

std::string encode_event_json_line(const SocketdEvent& event) {
  std::string out;
  out.reserve(512);

  out += "{";

  out += "\"Type\":\"";
  out += json_escape(event.type);
  out += "\",";

  out += "\"Action\":\"";
  out += json_escape(event.action);
  out += "\",";

  out += "\"Actor\":{";
  out += "\"ID\":\"";
  out += json_escape(event.actor_id);
  out += "\",";
  out += "\"Attributes\":{";

  for (std::size_t i = 0; i < event.attributes.size(); ++i) {
    if (i != 0) {
      out += ",";
    }

    out += "\"";
    out += json_escape(event.attributes[i].key);
    out += "\":\"";
    out += json_escape(event.attributes[i].value);
    out += "\"";
  }

  out += "}";
  out += "},";

  out += "\"scope\":\"local\",";

  out += "\"time\":";
  out += std::to_string(event.time);
  out += ",";

  out += "\"timeNano\":";
  out += std::to_string(event.time_nano);

  out += "}\n";
  return out;
}

}  // namespace

void record_socketd_event(const std::string& type,
                          const std::string& action,
                          const std::string& actor_id,
                          const SocketdEventAttributes* attributes,
                          std::size_t attribute_count) {
  SocketdEvent event;
  event.type = type;
  event.action = action;
  event.actor_id = actor_id;
  event.time = unix_time_seconds_now();
  event.time_nano = unix_time_nanos_now();

  event.attributes.reserve(attribute_count);
  for (std::size_t i = 0; i < attribute_count; ++i) {
    event.attributes.push_back(EventAttribute{
        attributes[i].key,
        attributes[i].value,
    });
  }

  if (g_socketd_events.size() >= kMaxSocketdEvents) {
    g_socketd_events.pop_front();
  }

  g_socketd_events.push_back(std::move(event));
}

bool request_event_log_stream_from_core(
    const EventsRequest& request,
    std::string& stream_out,
    std::string& error) {
  error.clear();
  stream_out.clear();

  /*
   * TODO(socketd-core-events):
   * This endpoint currently exposes extension-owned daemon events from the
   * socketd-local event journal. When the socket extension has a validated
   * need for core-runtime lifecycle events, merge in a backend-provided event
   * stream behind this same socketd-owned seam.
   */
  std::int64_t since = 0;
  std::int64_t until = 0;

  if (!parse_optional_epoch_seconds(request.since, since, error)) {
    return false;
  }

  if (!parse_optional_epoch_seconds(request.until, until, error)) {
    return false;
  }

  if (since > 0 && until > 0 && since > until) {
    error = "event 'since' timestamp is after 'until'";
    return false;
  }

  for (const SocketdEvent& event : g_socketd_events) {
    if (!event_in_window(event, since, until)) {
      continue;
    }

    stream_out += encode_event_json_line(event);
  }

  return true;
}

}  // namespace droidspaces::socketd
