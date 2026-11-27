# CPU Pinning in Droidspaces

Engineering report, November 27, 2026. The change was developed against commit `77e63323` and tested with a local Droidspaces `v7.2.0` build.

Droidspaces can now limit a container to explicit logical CPU IDs or select physical cores by class. The original target is an OpenWrt router container that runs its services on efficiency cores. The C runtime stores CPU placement as a bitmask. The live OpenWrt test confirmed placement through idle periods, local DNS traffic, service restart, CPU load, and an internal reboot. That test used explicit IDs on a homogeneous Intel host. Mixed core selection passed synthetic topology tests, and actual efficiency-core placement and power savings still need target hardware.

## User interface and scope

| CLI setting | Behavior |
| --- | --- |
| `--cpuset-cpus=0-3,6` | Select logical CPU IDs. A comma-separated list can contain individual IDs and inclusive ranges. |
| `--cpu-cores=1P,2E` | Select one physical performance core and two physical efficiency cores. Include the identifiable logical siblings of each selected core. |
| `--cpu-cores=2E` | Select two efficiency cores. This is the intended OpenWrt setting on a host that exposes the required topology. |
| `--cpuset-cpus=` or `--cpu-cores=` | Clear both placement modes for the next start. |
| `--cpus=0.5` | Limit CPU time to half a core's worth. A time quota can be combined with either placement mode. |

Pass a placement setting to `start` or `restart`. Either mode clears the other when supplied on the CLI, so the last placement option wins. The settings persist in `container.config`. `droidspaces check` reports whether the host exposes the cpuset controller, and `info` displays the controller's effective CPU list. `--json info` includes that list as `cpuset_cpus`, or an empty string when there is no placement setting or the controller read fails. See [CLI options](../src/cli/options.c), [capability checks](../src/cli/check.c), and [container info](../src/container/info.c).

The implementation is in the C backend. The final diff contains no Android front-end files. The existing app writes a fresh config from fields it knows, so saving settings through the current app can remove these new CLI-only keys. This follows from the app's config writer and was not device-tested. See [Android config writer](../Android/app/src/main/java/com/droidspaces/app/util/ContainerManager.kt).

## Runtime representation and lifecycle

The placement field in `struct ds_config` is `cpu_set_t cpuset_cpus`. Two integers hold requested P and E core counts. A separate flag records malformed saved input, allowing the config to be loaded and repaired while preventing a subsequent boot without the intended restriction. The CPU-list parser commits a complete mask only on success. Formatting emits sorted ranges from the mask. The bounded CPU ID range comes from `CPU_SETSIZE`, which was 1,024 in the tested build. See [config structure](../src/include/config/config.h), [parser](../src/config/parse.c), and [config loader](../src/config/load.c).

Text remains part of the interfaces. Config files and the monitor's command protocol serialize `cpuset_cpus` as a CPU list. The monitor caches its serialized reply, so a textual copy can remain in that reply after startup. Scheduling and cgroup setup use the mask in `ds_config` directly. Temporary buffers handle kernel cgroup files and display. The saved config retains `cpu_p_cores` and `cpu_e_cores` alongside the resolved list when class counts are set. See [config serialization](../src/config/save.c) and [monitor reply](../src/container/command.c).

1. `start_rootfs_locked()` resolves requested classes and validates the resulting mask before rootfs preparation. The preflight checks controller support, rejects IDs absent from the host, and requires at least one selected CPU online.
2. The monitor resolves class requests again during per-boot cgroup setup and applies the mask before forking the intermediate process that joins the cgroups and launches init. Children inherit that placement. `run` and `enter` join the cgroups before entering the container namespaces. With pinning configured, a reported attachment failure aborts the launch or session.
3. A normal restart reloads the host config and reapplies CLI overrides. An internal reboot clears the placement fields before reloading the host config and rebuilding the cgroups. A class request is therefore resolved against the topology available at boot. The monitor publishes the booted config so normal by-name operations can use the actual selection independently of edits to the saved file.

An operational consequence of the existing restart order is that hardware validation runs after the old instance has stopped. If the replacement placement is unavailable, the container can remain stopped. See [start](../src/container/start.c), [restart](../src/container/restart.c), [monitor lifecycle](../src/monitor/monitor.c), and [session launch](../src/container/exec.c).

## Physical core selection

On Intel hybrid hosts the selector reads the kernel's `cpu_core/cpus` and `cpu_atom/cpus` performance monitoring unit (PMU) lists. Those lists classify logical CPUs as P or E. CPUs outside both lists, including CPUs reported only in a separate low-power PMU class, are outside the automatic selection. On Arm and AArch64 the selector reads `cpu_capacity` for all present CPUs and requires differing capacities. E means the lowest capacity tier and P means the highest. For a three-tier phone, the middle tier remains available via explicit IDs. This is a classification convention based on scheduler performance capacity. Energy efficiency requires a separate measurement. See [selector implementation](../src/cgroup/cpu_cores.c), [Intel hybrid PMU mapping](https://github.com/torvalds/linux/blob/master/arch/x86/events/intel/core.c), and [Linux capacity-aware scheduling](https://docs.kernel.org/scheduler/sched-capacity.html).

The resolver groups sibling threads using `thread_siblings_list`, with physical package and core IDs as a fallback. A physical core needs at least one online thread to count. Identifiable present siblings can be included even when offline. The candidates are ordered by package ID, core ID, and logical CPU ID. Selection is deterministic on unchanged topology. CPU IDs and clock frequencies do not supply the class. If topology is unavailable, siblings conflict, or the request exceeds the number of eligible cores, startup fails. Explicit IDs remain available when automatic classification is unsupported.

For example, the Intel C fixture resolves `1P,2E` to logical IDs `1,3-5`. IDs 1 and 3 are sibling threads of the same physical P-core. IDs 4 and 5 are separate E-cores. The four logical IDs therefore represent three physical cores. See [topology fixtures](../tests/test_cpu_cores.c).

## Kernel enforcement and reporting

The cpuset controller is the enforcement point. An ordinary process affinity change is filtered through the task's cpuset, which also applies to its descendants. On cgroup v2, Droidspaces enables `cpuset` in the parent subtrees and writes `cpuset.cpus` for the container. Attached sessions use the existing `ds-enter` leaf. On v1, the code finds the host hierarchy that owns the controller, seeds memory nodes from the parent, and handles both `cpuset.cpus` and Android's `noprefix` `cpus` spelling. It reads the effective CPU set where the kernel exposes it and falls back to the configured CPU list on older v1 kernels. See [cgroup lifecycle](../src/cgroup/lifecycle.c), [cpuset setup](../src/cgroup/cpuset.c), and [Linux v1 cpusets](https://docs.kernel.org/admin-guide/cgroup-v1/cpusets.html).

The readback check requires a nonempty effective mask contained in the request. This matters on v2 because the kernel can use the parent's effective CPUs when none of the requested CPUs can be granted. A narrower effective mask is accepted, so a selected offline CPU or an ancestor restriction can reduce the effective allocation. Class counts describe the requested placement. For operational verification, compare the effective list from `info` and the affinity of guest processes against the intended logical IDs. The controller can change the effective mask on CPU hotplug, and the selector runs at boot rather than following changes continuously. If every selected CPU goes offline, the kernel may use other CPUs to keep tasks runnable. See [Linux v2 cpuset interface](https://docs.kernel.org/admin-guide/cgroup-v2.html#cpuset-interface-files).

CPU quotas still use their existing cgroup controls. When pinning is set, the backend skips the quota-only synthetic CPU topology and the affinity narrowing used to represent a quota as a CPU count. Guest tools can then refer to the real CPU IDs, including a noncontiguous set. Inspect the task's `Cpus_allowed_list` for its current affinity. The cpuset does not reserve host cores or move interrupts. The supervising monitor and host-side network helpers remain outside the container's payload cgroup. See [CPU virtualization](../src/virt/vproc.c) and [container CPU count](../src/virt/cpu.c).

## Validation results

| Check | Result and coverage |
| --- | --- |
| Native build and formatting | The x86_64 musl build passed with the repository's compiler flags. `make format` and `git diff --check` passed. |
| C topology fixtures | `make test-cpu-cores` passed. Fixtures exercised strict parsing, SMT grouping, offline siblings, deterministic selection, absent or insufficient classes, and two-tier and three-tier Arm capacities. |
| Live CPU-ID regression suite | Ten tests passed against the installed daemon in 21.809 seconds, with no errors, failures, or skips. Coverage included init and child affinity, `run`, `enter`, `taskset` confinement, quota plus pinning, restart, clearing, reset, and invalid IDs and saved config. The Alpine fixture included `taskset` from util-linux. |
| Forced v1 guest mode | The guest started with `--force-cgroupv1` and was pinned. The host kernel had `CONFIG_CPUSETS=y` and `CONFIG_CPUSETS_V1` disabled, so this check still used the v2 cpuset controller for placement. The v1 file formats and failure paths were exercised with simulated controller files, including Android's alternate names. |
| Unknown P/E topology | On the i5-9500, a `--cpu-cores=2E` start produced the expected diagnostic directing the operator to `--cpuset-cpus`. All six host CPUs reported the same capacity. The harness recorded the diagnostic. |

### OpenWrt fixture and observations

The host was x86_64 Linux `7.0.0-43-generic` on an Intel Core i5-9500. The guest rootfs was the official OpenWrt `25.12.6` generic x86/64 tarball, revision `r33311-981c978eff`. The SHA-256 matched the published `d09cb68dd78a7f7b1c4ed788ad42162e07ec094b20f66b382cd4f91c9443970f`. The fixture ran as `openwrt-cpuset-test` with `--net=none` and `--cpuset-cpus=0-1`. See [OpenWrt release artifacts](https://downloads.openwrt.org/releases/25.12.6/targets/x86/64/).

The generic image needed a guest-only init wrapper to create runtime directories under `/tmp` before executing `/sbin/procd`. The test also seeded UCI system and loopback config. The stock dnsmasq service declared an OpenWrt jail and initially exited with status 1. The final test disabled that service and enabled a minimal procd-managed dnsmasq on loopback, serving `cpuset.test` as `192.0.2.1`. The result covers that service profile and local queries. It does not establish stock jailed-service compatibility, DHCP, forwarding, or NAT performance. See [OpenWrt init sequence](https://github.com/openwrt/procd/blob/master/initd/init.c).

| Phase | Recorded result |
| --- | --- |
| Boot and idle | `ubus call system board` and `ubus call system info` returned data, and `uci show system` returned the fixture config. Host-side affinity captures five seconds apart included procd, ubusd and netifd. |
| DNS traffic | The local DNS query returned the expected address. A loop of 100 `nslookup` calls completed successfully. |
| Service restart | The test DNS process changed from PID `409215` to `409395`. Both instances were observed with allowed CPUs `0-1`. |
| CPU load | The guest ran a repeated `dd` and `sha256sum` pipeline. Five snapshots captured 15 threads each, including both workload processes. Their reported processor IDs and affinity stayed within CPUs `0-1`. |
| Guest reboot | procd changed from PID `406608` to `409893`, and ubus and the DNS response returned. The replacement dnsmasq was PID `410709`. `info --json` reported `cpuset_cpus` as `0-1`. |

The test read host `/proc` cgroup membership and `sched_getaffinity()` for each sampled thread. Across 11 snapshots and 143 thread observations, every recorded allowed set was exactly `{0, 1}`. These are point-in-time samples. The saved service inventory was taken before configuring the final DNS profile, so process snapshots and DNS responses are the evidence for its final state.

### Remaining target validation

The live host has six homogeneous cores. The OpenWrt workload confirms that a real service follows an explicit cpuset, while the C fixtures confirm how mixed classes resolve to IDs. An Intel hybrid PC and a heterogeneous Android device are needed to join those two results on real hardware. A v1 cpuset host, including one with Android's mount naming, is needed for live v1 coverage. The project retains its Linux 3.10 compatibility floor, and this work did not validate that floor on a device.

On the target, run OpenWrt with `--cpu-cores=2E`, verify that the effective IDs match the identified efficiency cores, and repeat the affinity checks during service restart, guest reboot, and representative router traffic. A mixed request such as `1P,2E` should also be checked against the physical topology and SMT siblings. Compare energy over the same idle and traffic intervals under unrestricted and E-only placement, holding link, workload, and device conditions fixed. Record throughput and latency with power so a drop in useful work does not look like an efficiency gain. Host IRQ and softirq processing, host daemons, and CPU hotplug can affect the result independently of guest placement.

## Reproduction and local evidence

```sh
make native
make test-cpu-cores

# Inspect the running OpenWrt fixture on the test host.
sudo droidspaces -n openwrt-cpuset-test --json info
sudo droidspaces -n openwrt-cpuset-test run -- ubus call system board
sudo droidspaces -n openwrt-cpuset-test run -- nslookup cpuset.test 127.0.0.1
```

The live regression suite expects a daemon serving the build under test and a compatible Alpine test rootfs. With the daemon accessible to the invoking user, run `python3 -m unittest discover -s tests -p test_cpuset.py -v`. Use the suite's `DS_TEST_ROOTFS` option to supply a rootfs with `taskset` for affinity widening coverage. See [live regression tests](../tests/test_cpuset.py) and [test environment settings](../tests/dstest.py).

The recorded log is `/tmp/droidspaces-openwrt-cpuset/cpu-id-tests.log`. The detailed host-local record is `/tmp/droidspaces-openwrt-cpuset/report.json`. The OpenWrt commands and assertions are in `/tmp/droidspaces-openwrt-cpuset/final-profile.py`, which expects the prepared fixture and installed daemon.

The installed binary and daemon executable matched the tested build with SHA-256 `44b6f96059f1cb3e12bbf2ed216ed341e461066107830ea1d23b5ae68bdc8483`. The original `v7.1.0` binary is backed up at `/var/backups/droidspaces-before-cpuset-20261126T173832Z/droidspaces`. The initial CPU-ID implementation is backed up at `/var/backups/droidspaces-before-core-mask`. The last recorded host check had only the OpenWrt fixture running, with the earlier Alpine fixture stopped.
