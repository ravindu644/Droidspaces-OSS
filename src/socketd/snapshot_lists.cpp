#include "snapshot_lists.h"

namespace droidspaces::socketd {

bool request_image_list_json_from_core(std::string& json_out,
                                       std::string& error) {
  error.clear();

  /*
   * TODO(socketd-snapshot-images):
   * This is a deliberate dummy response. Portainer's snapshot pass requests
   * /images/json, but Droidspaces has not yet decided whether Docker-style
   * image inventory is meaningful to expose. Keep this extension-local until
   * a socketd-owned need for core data is established.
   */
  json_out = "[]\n";
  return true;
}

bool request_volume_list_json_from_core(std::string& json_out,
                                        std::string& error) {
  error.clear();

  /*
   * TODO(socketd-snapshot-volumes):
   * This is a deliberate dummy response. The Docker-compatible /volumes route
   * returns an object, not a bare array, so preserve that shape while exposing
   * no volume inventory.
   */
  json_out = "{\"Volumes\":[],\"Warnings\":[]}\n";
  return true;
}

bool request_network_list_json_from_core(std::string& json_out,
                                         std::string& error) {
  error.clear();

  /*
   * TODO(socketd-snapshot-networks):
   * This is a deliberate dummy response. Portainer requests /networks during
   * its snapshot pass; return a structurally valid empty list and avoid
   * projecting Docker network semantics into the Droidspaces core.
   */
  json_out = "[]\n";
  return true;
}

}  // namespace droidspaces::socketd
