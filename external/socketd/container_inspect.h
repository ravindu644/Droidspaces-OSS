#pragma once

#include <string>

namespace droidspaces::socketd {

/*
 * Socketd-owned renderer for Docker-compatible container inspect payloads.
 * The backend supplies Droidspaces facts; this layer fills the broad Docker
 * object skeleton with stable placeholders for unsupported Docker-only fields.
 */
bool request_container_inspect_json_from_core(const std::string& ref,
                                              std::string& json_out,
                                              bool& not_found,
                                              std::string& error);

} // namespace droidspaces::socketd
