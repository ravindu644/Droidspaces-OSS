#include "container_list.h"

namespace droidspaces::socketd {

bool request_container_list_json_from_core(
    const ContainerListRequest& request,
    std::string& json_out,
    std::string& error) {
  (void)request;
  error.clear();

  /*
   * TODO(socketd-core-bridge):
   * Replace this dummy payload with a request to the Droidspaces core through
   * the private socketd backend protocol once Portainer-side behavior for the
   * public /containers/json endpoint has been confirmed.
   *
   * The HTTP layer must continue to depend on this socketd-owned seam, not on
   * core runtime internals directly.
   */
  json_out = "[]\n";
  return true;
}

}  // namespace droidspaces::socketd
