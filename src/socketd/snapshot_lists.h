#pragma once

#include <string>

namespace droidspaces::socketd {

/*
 * Extension-owned seams for Portainer snapshot inventory probes.
 *
 * Images are rendered from typed backend pseudo-image records. Volumes and
 * networks remain socketd-owned compatibility projections: Droidspaces has no
 * Docker-managed named-volume model, while the network list is a fixed public
 * facade over Droidspaces networking modes.
 */

bool request_image_list_json_from_core(std::string& json_out,
                                       std::string& error);

bool request_volume_list_json_from_core(std::string& json_out,
                                        std::string& error);

bool request_network_list_json_from_core(std::string& json_out,
                                         std::string& error);

}  // namespace droidspaces::socketd
