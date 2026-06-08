#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace droidspaces::socketd {

struct CapabilitiesResult {
  std::uint32_t mask = 0;
};

struct ContainerPortResult {
  std::uint16_t host_port = 0;
  std::uint16_t host_port_end = 0;
  std::uint16_t container_port = 0;
  std::uint16_t container_port_end = 0;
  std::uint8_t proto = 0; /* 0 = tcp, 1 = udp */
};

struct InspectEnvResult {
  std::string key;
  std::string value;
};

struct InspectBindResult {
  std::string source;
  std::string destination;
  bool read_only = false;
};

struct ContainerInspectResult {
  std::string name;
  std::string uuid;
  std::string rootfs_path;
  std::string image_ref;
  std::string hostname;
  std::string nat_ip;
  std::string custom_init;
  std::string dns_servers;
  std::int32_t pid = 0;
  std::uint8_t net_mode = 0; /* 0 = host, 1 = nat, 2 = none */
  std::int64_t started_at = 0;
  std::int64_t memory_limit = 0;
  std::int64_t cpu_quota = 0;
  std::int64_t cpu_period = 0;
  std::int64_t pids_limit = 0;
  std::int32_t privileged_mask = 0;
  bool foreground = false;
  bool volatile_mode = false;
  bool force_cgroupv1 = false;
  bool disable_ipv6 = false;
  bool android_storage = false;
  bool selinux_permissive = false;
  bool hw_access = false;
  bool gpu_mode = false;
  bool termux_x11 = false;
  bool block_nested_ns = false;
  bool is_img_mount = false;
  std::uint16_t env_total_count = 0;
  std::uint16_t bind_total_count = 0;
  std::uint16_t port_total_count = 0;
  std::vector<InspectEnvResult> env;
  std::vector<InspectBindResult> binds;
  std::vector<ContainerPortResult> ports;
};

struct ContainerRecordResult {
  std::string name;
  std::string uuid;
  std::string rootfs_path;
  std::string hostname;
  std::string nat_ip;
  std::string custom_init;
  std::int32_t pid = 0;
  std::uint8_t net_mode = 0; /* 0 = host, 1 = nat, 2 = none */
  std::int64_t started_at = 0;
  std::vector<ContainerPortResult> ports;
};

struct InfoResult {
  std::uint32_t containers_total = 0;
  std::uint32_t containers_running = 0;
  std::uint32_t containers_stopped = 0;
};

struct ImageRecordResult {
  std::string name;
  std::string rootfs_path;
  std::string uuid;
  bool is_running = false;
  std::int64_t created_at = 0;
};

struct CoreEventResult {
  std::int64_t time = 0;
  std::int64_t time_nano = 0;
  std::string type;
  std::string action;
  std::string actor_id;
  std::string actor_name;
};

struct LifecycleResult {
  bool not_found = false;
  bool already_running = false;
  bool already_stopped = false;
};

class BackendClient {
public:
  BackendClient() = default;

  bool ping(std::string &error) const;
  bool capabilities(CapabilitiesResult &out, std::string &error) const;

  bool list_containers(bool include_all,
                       std::vector<ContainerRecordResult> &out,
                       std::string &error) const;

  bool inspect_container(const std::string &ref,
                         ContainerInspectResult &out,
                         bool &not_found,
                         std::string &error) const;

  bool info(InfoResult &out, std::string &error) const;

  bool list_images(std::vector<ImageRecordResult> &out,
                   std::string &error) const;

  bool poll_events(std::int64_t since, std::vector<CoreEventResult> &out,
                   std::string &error) const;

  bool start_container(const std::string &ref, LifecycleResult &out,
                       std::string &error) const;
  bool stop_container(const std::string &ref, int timeout_seconds,
                      LifecycleResult &out, std::string &error) const;
  bool restart_container(const std::string &ref, int timeout_seconds,
                         LifecycleResult &out, std::string &error) const;

private:
  bool lifecycle_request(std::uint16_t opcode, const std::string &ref,
                         int timeout_seconds, LifecycleResult &out,
                         std::string &error) const;

  bool request(std::uint16_t opcode, const void *payload,
               std::uint32_t payload_len, std::uint16_t &status_out,
               std::string &payload_out, std::string &error) const;
};

} // namespace droidspaces::socketd
