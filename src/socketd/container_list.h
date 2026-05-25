#pragma once

#include <string>

namespace droidspaces::socketd {

struct ContainerListRequest {
  /*
   * Docker-compatible public API spelling:
   *
   *   GET /containers/json?all=1
   *
   * The extension keeps this as a semantic flag rather than exposing the
   * raw HTTP query string to any eventual core-side implementation.
   */
  bool include_all = false;
};

/*
 * Socketd-owned seam for obtaining the Docker-compatible container list
 * payload. The HTTP server remains independent of backend protocol details;
 * this layer requests typed core records through BackendClient and renders the
 * public JSON shape expected by Docker-compatible consumers.
 */
bool request_container_list_json_from_core(
    const ContainerListRequest& request,
    std::string& json_out,
    std::string& error);

}  // namespace droidspaces::socketd
