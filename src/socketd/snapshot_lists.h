#pragma once

#include <string>

namespace droidspaces::socketd {

/*
 * Extension-owned seams for Portainer snapshot inventory probes.
 *
 * These functions intentionally return dummy payloads for now. Portainer has
 * been observed requesting images, volumes, and networks during its Docker
 * snapshot pass after /containers/json succeeds.
 *
 * No core-engine change is warranted at this stage: first we confirm that
 * Portainer's UI settles when these discovery probes receive structurally
 * valid empty responses.
 */

bool request_image_list_json_from_core(std::string& json_out,
                                       std::string& error);

bool request_volume_list_json_from_core(std::string& json_out,
                                        std::string& error);

bool request_network_list_json_from_core(std::string& json_out,
                                         std::string& error);

}  // namespace droidspaces::socketd
