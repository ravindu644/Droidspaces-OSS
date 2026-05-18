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
 * Socketd-owned seam for obtaining the container-list payload.
 *
 * For the current extension-only bring-up step this is deliberately a dummy:
 * it returns an empty Docker-shaped JSON list so Portainer can advance to its
 * next probe. Once that behavior is confirmed, this function becomes the
 * place where socketd requests real container data from the core bridge.
 */
bool request_container_list_json_from_core(
    const ContainerListRequest& request,
    std::string& json_out,
    std::string& error);

}  // namespace droidspaces::socketd
