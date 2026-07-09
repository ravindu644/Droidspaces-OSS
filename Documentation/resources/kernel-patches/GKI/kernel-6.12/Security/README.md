# Security Patches and Kernel Configuration Mapping (GKI kernel-6.12)

The patches in this directory fix security vulnerabilities in specific kernel subsystems. **A patch is only needed when its corresponding kernel configuration option (CONFIG) is enabled**, because only then is the affected code compiled into the kernel. If a CONFIG is disabled, the matching patch can be skipped.

## Mapping Table

| Patch | Affected files | Required CONFIG | Description |
| --- | --- | --- | --- |
| `act_pedit.patch` | `net/sched/act_pedit.c`, `include/net/tc_act/tc_pedit.h` | `CONFIG_NET_ACT_PEDIT` (depends on `CONFIG_NET_SCHED`, `CONFIG_NET_CLS_ACT`) | Out-of-bounds read/write fix in the tc `pedit` traffic-control action |
| `bad_epoll.patch` | `fs/eventpoll.c` | `CONFIG_EPOLL` | Use-after-free fix for `struct file` / `eventpoll` in epoll |
| `cifswitch.patch` | `fs/smb/client/cifs_spnego.c` | `CONFIG_CIFS` + `CONFIG_CIFS_UPCALL` | CIFS SPNEGO upcall key description validation |
| `copy_fail.patch` | `crypto/af_alg.c` | `CONFIG_CRYPTO_USER_API` (and sub-options such as `CONFIG_CRYPTO_USER_API_SKCIPHER` / `AEAD`) | SGL count/offset fix in the AF_ALG userspace crypto interface. No need for Droidspaces as it has secomp blocking AF_ALG |
| `dirty_frag_esp.patch` | `net/ipv4/esp4.c`, `net/ipv6/esp6.c`, `net/ipv4/ip_output.c`, `net/ipv6/ip6_output.c` | `CONFIG_INET_ESP` and/or `CONFIG_INET6_ESP` (IPsec ESP) | Shared-fragment handling fix in the ESP input path |
| `dirty_frag_rxrpc.patch` | Same ESP files + `net/rxrpc/*` | `CONFIG_AF_RXRPC` (also includes the ESP changes above, i.e. `CONFIG_INET_ESP` / `CONFIG_INET6_ESP`) | Shared-fragment vulnerability fix triggered via RxRPC |
| `fragnesia.patch` | `net/core/skbuff.c` | `CONFIG_NET` (core networking, effectively always enabled) | `SKBFL_SHARED_FRAG` flag propagation fix in `skb_try_coalesce` |
| `pintheft.patch` | `net/rds/message.c` | `CONFIG_RDS` | Pinned-page leak fix on the RDS zerocopy failure path |
| `slab_cross_cache_confusion.patch` | `net/core/skbuff.c` | `CONFIG_NET` (core networking, effectively always enabled) | Cross-cache confusion fix for the skb small head cache |
| `ssh_keysign_pwn.patch` | `kernel/ptrace.c`, `kernel/exit.c`, `include/linux/sched.h` | None (ptrace is a core feature, always compiled) | Preserve `dumpable` state after process exit; fixes a ptrace access-check bypass |

## Usage Notes

1. Before applying a patch, check whether the corresponding CONFIG is enabled (`=y` or `=m`) in the target kernel's `.config` (or `defconfig`).
2. If a CONFIG is disabled (`# CONFIG_XXX is not set`), the vulnerable code is not compiled and the matching patch can be skipped.
3. Patches marked "core networking / core feature, effectively always enabled" (`fragnesia`, `slab_cross_cache_confusion`, `ssh_keysign_pwn`) should be applied under nearly all configurations.

## Droidspaces
1. For normal users using userns:
    copy_fail.patch (No need but for hardening, Optional)
    bad_epoll.patch (Essential)
    dirty_frag_esp.patch
    slab_cross_cache_confusion.patch
    act_pedit.patch
    ssh_keysign_pwn.patch
    (For most kernel build scripts, other patchs are not needed)
2. not using userns
    bad_epoll.patch (Essential)
    ssh_keysign_pwn.patch
