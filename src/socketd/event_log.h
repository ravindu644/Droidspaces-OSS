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
 *
 * The public /events endpoint exposes this journal in Docker-compatible
 * event-stream form. This deliberately covers only events owned by the socket
 * extension itself; core-engine events remain a later, separate backend seam.
 */
void record_socketd_event(const std::string& type,
                          const std::string& action,
                          const std::string& actor_id,
                          const SocketdEventAttributes* attributes,
                          std::size_t attribute_count);

/*
 * Return historical socketd-owned events matching the requested time window
 * as a Docker-compatible stream of JSON objects.
 */
bool request_event_log_stream_from_core(
    const EventsRequest& request,
    std::string& stream_out,
    std::string& error);

}  // namespace droidspaces::socketd
