#pragma once

#include <cstdint>
#include <string>

namespace droidspaces::socketd {

struct EventsRequest {
  std::string since;
  std::string until;
};

struct SocketdEventAttributes {
  std::string key;
  std::string value;
};

/*
 * Record a socketd-owned engine-style event in the extension-local journal.
 * These local daemon events are merged with backend core lifecycle events by
 * request_event_log_stream_from_core().
 */
void record_socketd_event(const std::string& type,
                          const std::string& action,
                          const std::string& actor_id,
                          const SocketdEventAttributes* attributes,
                          std::size_t attribute_count);

/*
 * Return events matching the requested time window as a Docker-compatible
 * stream of JSON objects. The response merges:
 *
 *   - socketd-owned local events recorded in this process, and
 *   - core lifecycle events fetched from the privileged backend bridge.
 */
bool request_event_log_stream_from_core(
    const EventsRequest& request,
    std::string& stream_out,
    std::string& error);

}  // namespace droidspaces::socketd
