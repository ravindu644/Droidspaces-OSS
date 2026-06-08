#include "container_inspect.h"

#include "backend_client.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <utility>

namespace droidspaces::socketd {
namespace {

constexpr const char* kDockerZeroTime = "0001-01-01T00:00:00Z";

std::string json_escape(const std::string& input) {
  std::string out;
  out.reserve(input.size());

  constexpr char kHex[] = "0123456789abcdef";

  for (unsigned char ch : input) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20) {
          out += "\\u00";
          out += kHex[(ch >> 4) & 0x0f];
          out += kHex[ch & 0x0f];
        } else {
          out += static_cast<char>(ch);
        }
        break;
    }
  }

  return out;
}

std::string rfc3339_utc(std::int64_t unix_seconds) {
  if (unix_seconds <= 0) {
    return kDockerZeroTime;
  }

  std::time_t value = static_cast<std::time_t>(unix_seconds);
  if (static_cast<std::int64_t>(value) != unix_seconds) {
    return kDockerZeroTime;
  }

  std::tm tm {};
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) || defined(__ANDROID__) || defined(__linux__)
  if (::gmtime_r(&value, &tm) == nullptr) {
    return kDockerZeroTime;
  }
#else
  const std::tm* tmp = std::gmtime(&value);
  if (tmp == nullptr) {
    return kDockerZeroTime;
  }
  tm = *tmp;
#endif

  char buffer[64] {};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
    return kDockerZeroTime;
  }

  return buffer;
}

const char* port_type(std::uint8_t proto) {
  return proto == 1u ? "udp" : "tcp";
}

const char* docker_network_mode(std::uint8_t mode) {
  switch (mode) {
    case 1u:
      return "bridge";
    case 2u:
      return "none";
    case 0u:
    default:
      return "host";
  }
}

std::string container_command(const ContainerInspectResult& record) {
  return record.custom_init.empty() ? "/sbin/init" : record.custom_init;
}

const char* container_state_status(const ContainerInspectResult& record) {
  return record.pid > 0 ? "running" : "exited";
}

void append_string_field(std::string& out,
                         const char* name,
                         const std::string& value) {
  out += '"';
  out += name;
  out += "\":\"";
  out += json_escape(value);
  out += '"';
}

void append_port_map_json(std::string& out,
                          const ContainerInspectResult& record,
                          bool include_host_bindings) {
  out += '{';

  for (std::size_t i = 0; i < record.ports.size(); ++i) {
    if (i != 0) {
      out += ',';
    }

    const ContainerPortResult& port = record.ports[i];
    out += '"';
    out += std::to_string(port.container_port);
    out += '/';
    out += port_type(port.proto);
    out += "\":";

    if (include_host_bindings) {
      out += "[{\"HostIp\":\"\",\"HostPort\":\"";
      out += std::to_string(port.host_port);
      out += "\"}]";
    } else {
      out += "{}";
    }
  }

  out += '}';
}

void append_config_json(std::string& out,
                        const ContainerInspectResult& record,
                        const std::string& command) {
  out += "\"Config\":{";
  append_string_field(out, "Hostname", record.hostname);
  out += ",\"Domainname\":\"\",";
  out += "\"User\":\"\",";
  out += "\"AttachStdin\":false,";
  out += "\"AttachStdout\":false,";
  out += "\"AttachStderr\":false,";
  out += "\"ExposedPorts\":";
  append_port_map_json(out, record, false);
  out += ',';
  out += "\"Tty\":false,";
  out += "\"OpenStdin\":false,";
  out += "\"StdinOnce\":false,";
  out += "\"Env\":[],";
  out += "\"Cmd\":[\"";
  out += json_escape(command);
  out += "\"],";
  append_string_field(out, "Image", record.image_ref.empty() ? record.rootfs_path : record.image_ref);
  out += ",\"Volumes\":{},";
  out += "\"WorkingDir\":\"\",";
  out += "\"Entrypoint\":[],";
  out += "\"OnBuild\":[],";
  out += "\"Labels\":{}";
  out += '}';
}

void append_host_config_json(std::string& out,
                             const ContainerInspectResult& record) {
  out += "\"HostConfig\":{";
  out += "\"Binds\":[],";
  out += "\"ContainerIDFile\":\"\",";
  out += "\"LogConfig\":{\"Type\":\"\",\"Config\":{}},";
  out += "\"NetworkMode\":\"";
  out += docker_network_mode(record.net_mode);
  out += "\",";
  out += "\"PortBindings\":";
  append_port_map_json(out, record, true);
  out += ',';
  out += "\"RestartPolicy\":{\"Name\":\"\",\"MaximumRetryCount\":0},";
  out += "\"AutoRemove\":false,";
  out += "\"VolumeDriver\":\"\",";
  out += "\"VolumesFrom\":[],";
  out += "\"CapAdd\":[],";
  out += "\"CapDrop\":[],";
  out += "\"CgroupnsMode\":\"\",";
  out += "\"Dns\":[],";
  out += "\"DnsOptions\":[],";
  out += "\"DnsSearch\":[],";
  out += "\"ExtraHosts\":[],";
  out += "\"GroupAdd\":[],";
  out += "\"IpcMode\":\"\",";
  out += "\"Cgroup\":\"\",";
  out += "\"Links\":[],";
  out += "\"OomScoreAdj\":0,";
  out += "\"PidMode\":\"\",";
  out += "\"Privileged\":false,";
  out += "\"PublishAllPorts\":false,";
  out += "\"ReadonlyRootfs\":false,";
  out += "\"SecurityOpt\":[],";
  out += "\"UTSMode\":\"\",";
  out += "\"UsernsMode\":\"\",";
  out += "\"ShmSize\":0,";
  out += "\"Runtime\":\"\",";
  out += "\"ConsoleSize\":[0,0],";
  out += "\"Isolation\":\"\",";
  out += "\"CpuShares\":0,";
  out += "\"Memory\":0,";
  out += "\"NanoCpus\":0,";
  out += "\"CgroupParent\":\"\",";
  out += "\"BlkioWeight\":0,";
  out += "\"BlkioWeightDevice\":[],";
  out += "\"BlkioDeviceReadBps\":[],";
  out += "\"BlkioDeviceWriteBps\":[],";
  out += "\"BlkioDeviceReadIOps\":[],";
  out += "\"BlkioDeviceWriteIOps\":[],";
  out += "\"CpuPeriod\":0,";
  out += "\"CpuQuota\":0,";
  out += "\"CpuRealtimePeriod\":0,";
  out += "\"CpuRealtimeRuntime\":0,";
  out += "\"CpusetCpus\":\"\",";
  out += "\"CpusetMems\":\"\",";
  out += "\"Devices\":[],";
  out += "\"DeviceCgroupRules\":[],";
  out += "\"DeviceRequests\":[],";
  out += "\"KernelMemory\":0,";
  out += "\"KernelMemoryTCP\":0,";
  out += "\"MemoryReservation\":0,";
  out += "\"MemorySwap\":0,";
  out += "\"MemorySwappiness\":0,";
  out += "\"OomKillDisable\":false,";
  out += "\"PidsLimit\":0,";
  out += "\"Ulimits\":[],";
  out += "\"CpuCount\":0,";
  out += "\"CpuPercent\":0,";
  out += "\"IOMaximumIOps\":0,";
  out += "\"IOMaximumBandwidth\":0,";
  out += "\"MaskedPaths\":[],";
  out += "\"ReadonlyPaths\":[]";
  out += '}';
}

void append_state_json(std::string& out,
                       const ContainerInspectResult& record,
                       const std::string& started_at) {
  out += "\"State\":{";
  out += "\"Status\":\"";
  out += container_state_status(record);
  out += "\",";
  out += "\"Running\":";
  out += record.pid > 0 ? "true" : "false";
  out += ',';
  out += "\"Paused\":false,";
  out += "\"Restarting\":false,";
  out += "\"OOMKilled\":false,";
  out += "\"Dead\":false,";
  out += "\"Pid\":";
  out += std::to_string(record.pid > 0 ? record.pid : 0);
  out += ',';
  out += "\"ExitCode\":0,";
  out += "\"Error\":\"\",";
  out += "\"StartedAt\":\"";
  out += json_escape(started_at);
  out += "\",";
  out += "\"FinishedAt\":\"";
  out += kDockerZeroTime;
  out += "\"";
  out += '}';
}

void append_networks_json(std::string& out,
                          const ContainerInspectResult& record) {
  out += "\"Networks\":{";

  if (record.net_mode == 1u) {
    out += "\"droidspaces-bridge\":{";
    out += "\"IPAMConfig\":{},";
    out += "\"Links\":[],";
    out += "\"Aliases\":[],";
    out += "\"NetworkID\":\"\",";
    out += "\"EndpointID\":\"\",";
    out += "\"Gateway\":\"\",";
    out += "\"IPAddress\":\"";
    out += json_escape(record.nat_ip);
    out += "\",";
    out += "\"IPPrefixLen\":0,";
    out += "\"IPv6Gateway\":\"\",";
    out += "\"GlobalIPv6Address\":\"\",";
    out += "\"GlobalIPv6PrefixLen\":0,";
    out += "\"MacAddress\":\"\",";
    out += "\"DriverOpts\":{}";
    out += '}';
  }

  out += '}';
}

void append_network_settings_json(std::string& out,
                                  const ContainerInspectResult& record) {
  out += "\"NetworkSettings\":{";
  out += "\"Bridge\":\"\",";
  out += "\"SandboxID\":\"\",";
  out += "\"HairpinMode\":false,";
  out += "\"LinkLocalIPv6Address\":\"\",";
  out += "\"LinkLocalIPv6PrefixLen\":0,";
  out += "\"Ports\":";
  append_port_map_json(out, record, true);
  out += ',';
  out += "\"SandboxKey\":\"\",";
  out += "\"SecondaryIPAddresses\":[],";
  out += "\"SecondaryIPv6Addresses\":[],";
  out += "\"EndpointID\":\"\",";
  out += "\"Gateway\":\"\",";
  out += "\"GlobalIPv6Address\":\"\",";
  out += "\"GlobalIPv6PrefixLen\":0,";
  out += "\"IPAddress\":\"";
  if (record.net_mode == 1u) {
    out += json_escape(record.nat_ip);
  }
  out += "\",";
  out += "\"IPPrefixLen\":0,";
  out += "\"IPv6Gateway\":\"\",";
  out += "\"MacAddress\":\"\",";
  append_networks_json(out, record);
  out += '}';
}

void append_inspect_json(std::string& out,
                         const ContainerInspectResult& record) {
  const std::string command = container_command(record);
  const std::string started_at = rfc3339_utc(record.started_at);
  const std::string created = started_at;

  out += '{';

  out += "\"AppArmorProfile\":\"\",";
  out += "\"Args\":[],";
  append_config_json(out, record, command);
  out += ',';
  out += "\"Created\":\"";
  out += json_escape(created);
  out += "\",";
  out += "\"Driver\":\"droidspaces\",";
  out += "\"ExecIDs\":[],";
  append_host_config_json(out, record);
  out += ',';
  out += "\"HostnamePath\":\"\",";
  out += "\"HostsPath\":\"\",";
  out += "\"LogPath\":\"\",";
  append_string_field(out, "Id", record.uuid);
  out += ',';
  append_string_field(out, "Image", record.image_ref.empty() ? record.rootfs_path : record.image_ref);
  out += ',';
  out += "\"MountLabel\":\"\",";
  out += "\"Name\":\"/";
  out += json_escape(record.name);
  out += "\",";
  append_network_settings_json(out, record);
  out += ',';
  append_string_field(out, "Path", command);
  out += ',';
  out += "\"ProcessLabel\":\"\",";
  out += "\"ResolvConfPath\":\"\",";
  out += "\"RestartCount\":0,";
  append_state_json(out, record, started_at);
  out += ',';
  out += "\"Mounts\":[]";

  out += "}\n";
}

}  // namespace

bool request_container_inspect_json_from_core(const std::string& ref,
                                              std::string& json_out,
                                              bool& not_found,
                                              std::string& error) {
  error.clear();
  not_found = false;

  BackendClient backend;
  ContainerInspectResult record;

  if (!backend.inspect_container(ref, record, not_found, error)) {
    return false;
  }

  std::string body;
  body.reserve(4096);
  append_inspect_json(body, record);

  json_out = std::move(body);
  return true;
}

}  // namespace droidspaces::socketd
