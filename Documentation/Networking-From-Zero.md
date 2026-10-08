<!--
title: Networking from zero
section: Guides
order: 5
desc: Every networking concept behind Droidspaces gateway mode, from the start: IP addresses, LAN, WAN, DHCP, DNS, NAT, bridges, veth pairs, network namespaces and OpenWRT.
keywords: droidspaces, networking, gateway, openwrt, nat, dhcp, dns, lan, wan, veth, bridge, namespace, linux, android
-->

# Networking from zero: understanding Droidspaces gateway mode

### Quick navigation

- [Part 1: The absolute basics](#part-1-the-absolute-basics)
    - [What is an IP address?](#what-is-an-ip-address)
    - [What is a network?](#what-is-a-network)
    - [LAN - Local Area Network](#lan---local-area-network)
    - [WAN - Wide Area Network](#wan---wide-area-network)
    - [Gateway](#gateway)
- [Part 2: How data actually gets delivered](#part-2-how-data-actually-gets-delivered)
    - [MAC address vs IP address](#mac-address-vs-ip-address)
    - [What is a packet?](#what-is-a-packet)
- [Part 3: DHCP (how you get an IP address)](#part-3-dhcp-how-you-get-an-ip-address)
- [Part 4: DNS (how names become addresses)](#part-4-dns-how-names-become-addresses)
- [Part 5: NAT (the magic your router does)](#part-5-nat-the-magic-your-router-does)
    - [NAT in Droidspaces](#nat-in-droidspaces)
    - [How NAT mode picks the WAN uplink (automatic)](#how-nat-mode-picks-the-wan-uplink-automatic)
    - [Pinning the uplink manually with --upstream](#pinning-the-uplink-manually-with---upstream)
    - [Use cases for --upstream](#use-cases-for---upstream)
- [Part 6: Bridges and virtual cables (Linux plumbing)](#part-6-bridges-and-virtual-cables-linux-plumbing)
    - [What is a network bridge?](#what-is-a-network-bridge)
    - [What is a veth pair?](#what-is-a-veth-pair)
    - [How NAT mode uses bridges and veths](#how-nat-mode-uses-bridges-and-veths)
- [Part 7: Network namespaces (how containers are isolated)](#part-7-network-namespaces-how-containers-are-isolated)
- [Part 8: OpenWRT and what it is](#part-8-openwrt-and-what-it-is)
- [Part 9: The new gateway mode - putting it all together](#part-9-the-new-gateway-mode---putting-it-all-together)
    - [Why does gateway mode exist?](#why-does-gateway-mode-exist)
    - [The architecture](#the-architecture)
    - [Step by step - what happens when you start a gateway-mode container](#step-by-step---what-happens-when-you-start-a-gateway-mode-container)
    - [What "lazy attachment" means](#what-lazy-attachment-means)
    - [Why resolv.conf is left alone in gateway mode](#why-resolvconf-is-left-alone-in-gateway-mode)
    - [Why bridge-nf-call-iptables is set to 0](#why-bridge-nf-call-iptables-is-set-to-0)
    - [Start order and automatic self-healing](#start-order-and-automatic-self-healing)
    - [What happens when containers stop](#what-happens-when-containers-stop)
- [Part 10: Gateway mode flags and configuration](#part-10-gateway-mode-flags-and-configuration)
    - [Required vs optional flags](#required-vs-optional-flags)
    - [What --gateway-net does](#what---gateway-net-does)
    - [What --gateway-iface does](#what---gateway-iface-does)
    - [The flag conflict you must avoid](#the-flag-conflict-you-must-avoid)
    - [IPv6 through the gateway](#ipv6-through-the-gateway)
    - [Validation rules and kernel requirements](#validation-rules-and-kernel-requirements)
- [Part 11: Comparing all networking modes](#part-11-comparing-all-networking-modes)
- [Part 12: Real-world use cases for gateway mode](#part-12-real-world-use-cases-for-gateway-mode)
- [Quick reference - terms](#quick-reference---terms)

---

## Part 1: The absolute basics

### What is an IP address?

Every device on a network needs an address, so other devices know where to send data. That address is its **IP address**.

It works like a postal address. To mail you a letter, someone needs your address. In the same way, your phone needs Google's address to send it data, and Google needs your phone's address to send the reply.

An IP address looks like this: `192.168.1.5`

It is four numbers (0-255) separated by dots. Each number is an **octet**.

### What is a network?

A **network** is a group of devices that can talk to each other directly.

Picture a room with 5 laptops on the same Wi-Fi router. Those 5 laptops are on the same network, and they can send files to each other without going through the internet.

### LAN - Local Area Network

**LAN** is the network *inside your home* (or office, or here, *inside the container world*).

It is "local" because the devices are physically nearby: your phone, laptop and smart TV, all on your home Wi-Fi router. They share one LAN and talk to each other directly.

LAN addresses usually look like:

- `192.168.x.x`
- `10.x.x.x`
- `172.16.x.x` to `172.31.x.x`

These are the **private IP ranges**. They are reserved for local networks and never used on the public internet.

### WAN - Wide Area Network

**WAN** is the network *outside your home*: the internet.

Your router has two sides:

- The **LAN side** faces your devices at home.
- The **WAN side** faces your internet provider (ISP).

Your ISP gives the router one public IP address for the WAN side. Every device in your home shares that one public IP to reach the internet.

```
[Your Phone]--+
[Your Laptop]-+--[Router]--[ISP]--[The Internet]
[Your TV]-----+
  (LAN side)     (WAN side)
```

### Gateway

A **gateway** is the device that connects two networks.

At home, the router *is* the gateway. Your phone's IP is `192.168.1.5` (LAN). When it wants to reach Google at `142.250.80.46` (WAN, the internet), it has no direct path. It sends the data to the gateway (the router), and the router forwards it to the internet.

**Rule:** every device on a LAN is configured with a "default gateway", the address it sends all traffic to when it has no more specific route.

---

## Part 2: How data actually gets delivered

### MAC address vs IP address

Networking uses *two* kinds of address:

| Type | Looks like | Purpose |
|---|---|---|
| **IP address** | `192.168.1.5` | Logical address - used for routing across networks |
| **MAC address** | `a4:c3:f0:12:34:56` | Physical address - used for delivery on the *same* network |

Think of it this way:

- The IP address is the **city and street**, used to navigate across the country.
- The MAC address is the **apartment number**, used once you reach the building.

When your laptop sends a packet to your router, it uses the router's MAC address, because they are on the same LAN. The router then uses IP addresses to decide where the packet goes next.

### What is a packet?

Data crossing a network is split into small chunks called **packets**. Each packet carries:

- Where it came from (source IP)
- Where it is going (destination IP)
- A small piece of the actual data

The destination reassembles the packets.

---

## Part 3: DHCP (how you get an IP address)

### The problem

Every device needs an IP address to join a network, and no two devices can have the same one. That would be two houses with the same postal address: mail gets lost.

You *could* assign a unique IP to every device by hand, but with 50 devices that is painful.

### The solution: DHCP

**DHCP** = Dynamic Host Configuration Protocol

One device (the **DHCP server**) hands out IP addresses to every new device that joins the network.

The exchange goes like this:

```
New Device:   "Hello? Anyone there? I just joined this network and I need an IP address."
DHCP Server:  "I heard you. Here, take 192.168.1.42. Also, your gateway is 192.168.1.1,
               and for DNS use 1.1.1.1. Your lease lasts 24 hours."
New Device:   "Got it, thanks!"
```

The new device now has everything it needs:

- Its own IP address
- The gateway address (where to send traffic)
- The DNS address (explained next)

At home, the **router runs the DHCP server** and hands out IPs to every device that connects.

In **Droidspaces NAT mode**, Droidspaces runs its own small DHCP server that gives the container its IP (in the `172.28.x.x` range). The IP is deterministic: it is derived from the container's name, saved to its config file and offered again on every boot, so a container keeps the same address across restarts.

---

## Part 4: DNS (how names become addresses)

### The problem

IP addresses are hard to remember. Nobody types `142.250.80.46` to visit Google. They type `google.com`.

Computers only understand IP addresses, so something has to turn human-readable names into IP addresses.

### The solution: DNS

**DNS** = Domain Name System

It is the internet's phone book. You give it a name (`google.com`) and it gives back an IP address (`142.250.80.46`).

The exchange:

```
Your Browser:   "What is the IP address of google.com?"
DNS Server:     "It is 142.250.80.46"
Your Browser:   "Thanks." [now connects to 142.250.80.46]
```

Every device is configured with a DNS server address. On most home networks the router *is* the DNS server: it forwards your queries to your ISP's DNS or to a public one like `1.1.1.1`.

In **Droidspaces NAT mode**, Droidspaces writes a `resolv.conf` inside the container that points at a DNS server (`1.1.1.1` and `8.8.8.8` by default, or whatever you pass with `--dns`). The same DNS servers are also advertised in the DHCP lease.

---

## Part 5: NAT (the magic your router does)

### The problem

Your ISP gives you *one* public IP address, but you have 10 devices at home. How do all 10 use the internet at once?

### The solution: NAT

**NAT** = Network Address Translation

Your router keeps a table. When a device on your LAN sends a packet to the internet, the router:

1. Rewrites the source IP from the device's private IP (`192.168.1.5`) to the router's public IP.
2. Records which device sent it.
3. When the reply arrives, rewrites the destination back to the device's private IP and forwards it.

To the internet, all your home devices look like *one device*: the router.

```
[Laptop: 192.168.1.5] --sends packet--> [Router]
                                          |
                                          | rewrites source to public IP
                                          v
                                     [Internet]
                                          |
                                          | reply comes back
                                          v
                                     [Router]
                                          |
                                          | rewrites destination back to 192.168.1.5
                                          v
                              [Laptop: 192.168.1.5]
```

### NAT in Droidspaces

In NAT mode, Droidspaces does for containers what your home router does for your devices:

- The container gets a private IP (`172.28.x.x`).
- Droidspaces installs iptables `MASQUERADE` rules (`MASQUERADE` is the Linux name for the NAT target), plus FORWARD-accept and MSS-clamp rules so traffic actually flows.
- The container can reach the internet, and the internet sees Android's IP, not the container's.
- Droidspaces runs an embedded DHCP server for the container and configures its DNS.
- The container also gets IPv6. Droidspaces announces a private prefix (`fd64:7370::/64`) with router advertisements, so the container configures its own address, and translates it on the way out with the IPv6 version of `MASQUERADE`. This needs the IPv6 NAT table in the kernel; without it the container is IPv4 only.
- On Android, a background route monitor finds the active internet uplink by reading the kernel's routing rules, and re-points container traffic as soon as the active network changes (for example, a handoff from Wi-Fi to mobile data).

### How NAT mode picks the WAN uplink (automatic)

A NAT container has to know *which* of Android's real interfaces has internet right now, so it can MASQUERADE through it. Phones make this hard. The active network moves between Wi-Fi, mobile data, USB-ethernet and VPN tunnels, and the interface names are unstable: the mobile-data interface can be `rmnet0` one minute and `rmnet8` after a reconnect.

By default Droidspaces handles this and **there is nothing to configure**:

- It reads the kernel's *own* answer to "which interface is the internet right now". On Android that is the policy-routing rule `netd` installs for the active default network. On desktop Linux it is the default route in the main routing table.
- A background **route monitor** subscribes to kernel routing events (rule, route, link and address changes). When the host switches networks, say you walk out of Wi-Fi range and it falls back to mobile data, or you plug a USB-ethernet dongle into a laptop, the monitor re-points the container's traffic straight away. No restart, no config.
- CLAT/464xlat interfaces (the `v4-rmnet...` interfaces phones create on IPv6-only mobile networks) are picked up automatically.

Automatic mode is the right choice for almost everyone. The rest of this section only matters if you want to *override* it.

### Pinning the uplink manually with `--upstream`

Sometimes you do **not** want the container to follow whatever network the host is on. You want its internet traffic to leave through one specific interface and stay there. That is what `--upstream` does.

Passing `--upstream` turns automatic detection off completely. The interfaces you list become the *only* candidates the container ever uses for WAN. It never moves to whatever the host marks as its active default network.

```bash
# Force the container's internet out through Wi-Fi, always
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=wlan0 start
```

You can list **several interfaces, comma-separated**, and use **wildcards** (`*`, `?`):

```bash
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=wlan0,rmnet* start
```

The list is **in priority order**. The route monitor walks it from the top and uses the first interface that is up and has internet. So `wlan0,rmnet*` means "prefer Wi-Fi, and if Wi-Fi is down, fall back to mobile data". When Wi-Fi comes back, it switches back. Failover stays strictly *inside your list*: it never falls back to an interface you did not list. That is the difference from auto mode. You decide the WAN, not the host.

A pinned interface that is missing when the container starts, or that disappears mid-session and comes back later, is handled too. The container has no WAN until one of your pinned interfaces is up, then it is wired up automatically.

> **Why wildcards matter on mobile data:** Android does not give the mobile-data interface a stable number. It can be `rmnet0`, `rmnet8` or `rmnet_data2`, and the number can change across reconnects. Pin a literal `rmnet0` and it breaks the next time the interface comes up under another name. Pin `rmnet*` and it keeps working.

### Use cases for `--upstream`

**1. Route the container's WAN through an Android VPN (`tun0`)**

Connect a VPN on the phone: ProtonVPN, WireGuard, OpenVPN, or any app that creates a `tun0` interface. Then pin the container to it:

```bash
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=tun0 start
```

All of the container's traffic now leaves through the VPN tunnel, and *only* the tunnel. If the VPN drops, `tun0` disappears and the container loses internet instead of leaking over your real connection. That is a killswitch with no extra setup.

**2. Container on mobile data while the phone stays on Wi-Fi**

Android can keep the cellular radio up while you are on Wi-Fi. Enable **"Mobile data always active"** in Developer Options, connect to Wi-Fi, then turn mobile data on. Both networks are now live. Pin the container to the mobile-data interface:

```bash
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=rmnet* start
```

The container's traffic goes out over mobile data while the rest of the phone stays on Wi-Fi. Useful for testing from a different IP or network, moving a container's bandwidth onto cellular, or running something on a separate connection from everything else on the phone.

---

## Part 6: Bridges and virtual cables (Linux plumbing)

One level deeper: how Linux connects containers to each other.

### What is a network bridge?

A **network bridge** works like a network switch. A physical switch is a box you plug several ethernet cables into, and every device connected to it can talk to the others.

A Linux **bridge** is a virtual switch, entirely in software. You create it with a command and then "plug" virtual network interfaces into it.

```
Physical world:           Linux world:
+--------------+          +--------------+
|   Switch     |          |   Bridge     |  (software, no physical box)
| port1  port2 |          | port1  port2 |
+--+------+---+          +--+------+---+
   |      |                  |      |
[PC1]  [PC2]           [veth1]  [veth2]   (virtual cables)
```

### What is a veth pair?

**veth** = virtual ethernet

A veth pair is two virtual network interfaces joined like a pipe. Whatever goes in one end comes out the other.

Think of it as a virtual ethernet cable with two plugs. One plug goes inside a container, and the other stays on the host (or goes into a bridge).

```
[Container netns]          [Host netns]
     eth0 --------------------- ds-veth0
  (plug inside container)   (plug on host side)
```

### How NAT mode uses bridges and veths

In Droidspaces NAT mode:

```
[Container netns]
     eth0 (e.g. 172.28.137.42)
      |
      | veth pair (virtual cable)
      |
[Host side]
     ds-v<PID> ---- ds-br0 (bridge, has IP 172.28.0.1)
                          |
                     iptables MASQUERADE
                          |
                      wlan0 / rmnet0
                     (Android's real network)
```

The bridge `ds-br0` holds the gateway IP `172.28.0.1`, which every NAT container uses as its default gateway. Its MAC is pinned from its name on every start, because an unpinned bridge borrows the lowest MAC among its ports and would change it whenever that container stops, leaving every other container with a stale ARP entry for the gateway. The veth pair is named after the container's init process ID: the host side is `ds-v<PID>`, and the container side starts as `ds-p<PID>` and is renamed to `eth0` inside the container.

Droidspaces runs a small per-container DHCP server on the container's host-side veth. The whole `172.28.0.0/16` subnet belongs to Droidspaces. The `172.28.0.x` row is reserved for the gateway itself, so containers always get an address from `172.28.1.x` to `172.28.254.x`, and all of it is NATed out through Android's real interface.

---

## Part 7: Network namespaces (how containers are isolated)

### What is a namespace?

Linux **namespaces** give a process its own isolated view of a system resource.

A **network namespace** is an isolated copy of the whole networking stack. It has its own:

- Network interfaces
- Routing table
- iptables rules
- Everything else networking-related

When Droidspaces starts a container, it creates a new network namespace for it, and the container lives there. It cannot see the host's network interfaces at all, only what Droidspaces puts into its namespace.

The veth pair is the link between the host's namespace and the container's:

- One end sits in the container's network namespace (as `eth0`).
- The other end stays in the host's network namespace, where Droidspaces connects it to a bridge.

---

## Part 8: OpenWRT and what it is

### What is OpenWRT?

**OpenWRT** is a Linux distribution built for routers. It usually runs on router hardware, but it can also run on an ordinary Linux system or inside a container.

A running OpenWRT provides:

- **netifd**, the network interface daemon (manages network interfaces, DHCP client/server, etc.)
- **dnsmasq**, a DNS and DHCP server
- **firewall3** or **nftables**, the firewall
- **LuCI**, a web UI for configuration
- Everything else a real router does, in software

So you can run OpenWRT in a Droidspaces container and it behaves like a real router: it manages networks, hands out DHCP leases, serves DNS, applies firewall rules, routes VPN traffic, and so on.

---

## Part 9: The new gateway mode - putting it all together

### Why does gateway mode exist?

In NAT mode, Droidspaces is the router and does everything. For most uses that is fine.

But you may want **OpenWRT to be the router** for other containers: OpenWRT's firewall rules, OpenWRT's DHCP, OpenWRT's VPN routing, with other containers (a Kali Linux container, say) sitting on OpenWRT's LAN and getting everything from it.

If Droidspaces also sets up NAT, DHCP and DNS for those containers, it *conflicts* with OpenWRT: two DHCP servers competing to hand out the address, two firewalls applying contradictory rules.

**Gateway mode avoids this.** Droidspaces steps back. It does only the L2 plumbing (the virtual cables and the switch) and leaves all the policy to OpenWRT: DHCP, DNS, firewall and routing.

### The architecture

```
Android host kernel
|
+-- wlan0 (Android's real Wi-Fi - WAN)
|
+-- [OpenWRT container - net=nat mode]
|    netns: owns eth0 (WAN, gets NAT from Droidspaces)
|           eth1 (LAN side - plugged into ds-lan bridge by gateway mode)
|    Runs: dnsmasq, netifd, firewall, VPN
|
+-- ds-lan (host bridge - NO IP address, just a switch)
|    |
|    +-- ds-g[hash] (veth host-side, connected to OpenWRT's netns as eth1)
|    +-- ds-c[pid]  (veth host-side, connected to Kali's netns as eth0)
|
+-- [Kali container - net=gateway mode]
     netns: owns eth0 (LAN side - plugged into ds-lan bridge)
     Gets DHCP from OpenWRT's dnsmasq
     Routing decisions made by OpenWRT
     Firewall rules applied by OpenWRT
```

### Step by step - what happens when you start a gateway-mode container

**Step 1: start OpenWRT first (in NAT mode)**

```bash
droidspaces --name=openwrt --rootfs=/data/openwrt --net=nat start
```

OpenWRT boots with:

- `eth0` on the WAN side (Droidspaces handles NAT for it)
- No LAN side yet. OpenWRT is waiting for one.

**Step 2: start Kali (in gateway mode)**

```bash
droidspaces --name=kali --rootfs=/data/kali --net=gateway --gateway=openwrt start
```

Droidspaces does the following, all plumbing and no policy:

1. Finds OpenWRT's running process ID, so it can reach its network namespace.
2. Creates a bridge called `ds-lan` on the host, with no IP address on it.
3. Disables `bridge-nf-call-iptables`, so Android's host firewall does NOT intercept traffic on this bridge and OpenWRT's firewall stays the only authority.
4. Creates a veth pair for OpenWRT's LAN side. One end goes into OpenWRT's netns (as `eth1`), the other plugs into the `ds-lan` bridge.
5. Creates a veth pair for Kali. One end goes into Kali's netns (as `eth0`), the other plugs into the `ds-lan` bridge.
6. Does NOT install NAT, DHCP, DNS or any firewall rules.

**Step 3: OpenWRT takes over**

OpenWRT's `netifd` sees `eth1` appear and configures it as the LAN interface.
OpenWRT's `dnsmasq` starts answering DHCP requests on `eth1`.

Kali's `eth0` sends a DHCP request, and OpenWRT's `dnsmasq` replies with:

- IP address: `192.168.1.100` (or whatever OpenWRT's DHCP range is)
- Gateway: `192.168.1.1` (OpenWRT itself)
- DNS: `192.168.1.1` (OpenWRT's dnsmasq)

Kali is now configured, with OpenWRT as its router.

**Step 4: traffic flows through OpenWRT**

When Kali reaches for the internet:

```
Kali eth0 --> ds-lan bridge --> OpenWRT eth1
                                      |
                              OpenWRT firewall rules applied here
                                      |
                              OpenWRT routes to eth0 (WAN)
                                      |
                              Droidspaces NAT (eth0 -> wlan0)
                                      |
                                  Android wlan0 --> Internet
```

OpenWRT's firewall sees all of Kali's traffic and can apply any rule a real router could: block sites, redirect through a VPN, shape bandwidth, log connections.

### What "lazy attachment" means

The gateway veth is "lazily attached":

- Starting OpenWRT does NOT give it an `eth1` straight away.
- `eth1` appears inside OpenWRT only **when the first gateway-mode container starts**.
- This is on purpose. OpenWRT boots with only its WAN side (`eth0`), and its LAN cable (`eth1`) is plugged in later, on demand.

It is the same as plugging a cable into a router's LAN port after the router is already running.

### Why resolv.conf is left alone in gateway mode

In NAT mode, Droidspaces writes `/etc/resolv.conf` inside the container, pointing at `1.1.1.1` or `8.8.8.8`.

In gateway mode, Droidspaces does NOT write a static `resolv.conf` unless you pass `--dns`. OpenWRT's `dnsmasq` gives the container its DNS server in the DHCP lease. If Droidspaces also wrote a `resolv.conf`, it would conflict with that: the container would use the wrong DNS and skip OpenWRT's DNS filtering and caching entirely.

How this is wired depends on the client's init system:

- **systemd containers:** `/etc/resolv.conf` is a symlink to `/run/systemd/resolve/resolv.conf`, which systemd-resolved fills from the DHCP lease.
- **non-systemd containers:** Droidspaces leaves `/etc/resolv.conf` alone, so the container's own DHCP client (udhcpc/dhclient) writes the nameserver from the gateway's lease. Earlier builds wrote a hardcoded `1.1.1.1`/`8.8.8.8` here, which silently bypassed the gateway's DNS. That is fixed. If a minimal rootfs ships no DHCP resolv.conf hook, pass `--dns` to set one explicitly.

### Why bridge-nf-call-iptables is set to 0

The bridge `ds-lan` carries traffic between OpenWRT and Kali. By default, Linux can pass bridged traffic through the host's iptables. Android's iptables rules, which may drop or NAT packets unexpectedly, would then interfere with traffic OpenWRT is supposed to manage.

Setting it to `0` tells Linux not to run iptables on bridged traffic. OpenWRT's firewall is then the *only* firewall that sees this traffic, which is what we want.

### Start order and automatic self-healing

All wiring for a gateway-mode client is done **from the host side** by one function, `gateway_wire_client()`. It makes sure the bridge and the gateway-side cable exist, then creates the client's app veth with its peer born inside the client's namespace as `eth0` (with a pinned MAC, brought up). The client's own boot code only brings up `lo`. Because the host owns every step, the same function wires a client whether it is starting or already running.

That leads to one simple rule, keyed on **whether the gateway is running**:

- **Gateway already running when a client starts** → the client is wired immediately (its own monitor calls `gateway_wire_client`).
- **Gateway not running when a client starts** → the client wires **nothing at all** (no bridge, no veth, no `eth0`) and boots. The work is left entirely to the gateway.

So healing is driven by **the gateway**, not the clients. On every boot cycle, the gateway container's monitor calls `ds_net_rewire_gateway_clients()`. It scans the running containers, finds those that delegate to this gateway, and runs `gateway_wire_client` for each, setting up the gateway-side `eth1` cable and every client's `eth0` in the gateway's *current* namespace. When the gateway **starts or reboots**, every running client is (re)wired **without restarting the client**.

Wiring nothing while the gateway is down, instead of half-wiring a bridge and a dangling veth, also closes a race. A client started before its gateway can have its gateway and LAN settings (`--gateway-net`, `--host-bridge`, …) edited before the gateway comes up, and the gateway then wires each client from that client's *current* config, never a stale one.

There is exactly **one actor** (the gateway) doing the wiring, so there is nothing to poll and no thundering herd. Wiring is serialised per segment with an advisory file lock, so concurrent client starts and the gateway's re-wire cannot race. Both `eth1` (gateway side) and each `eth0` (client side) keep a **stable MAC** and are created inside their namespace under their final name, in the one request that creates the pair. Nothing is moved or renamed afterwards: a rename is announced by the kernel before the device can be looked up by its new name, and a `netifd` inside the gateway that hears the announcement first fails to claim the device for good (seen on a 4.14 kernel, where that gap is tens of milliseconds).

### What happens when containers stop

Cleanup in gateway mode is deliberately minimal, in line with "plumbing only":

- **A client stops:** only that client's own veth is removed (gateway clients use the `ds-c<PID>` prefix, distinct from NAT's `ds-v<PID>`). The bridge and the gateway's `eth1` stay up, so other clients on the segment are not affected.
- **The last client stops while the gateway is still running:** the bridge is **kept**, not reaped. Tearing it down would flap the carrier on the gateway's live `eth1` and sometimes make netifd report "device initialization failed". An idle bridge with no IP does no harm, and the next client reuses it.
- **The gateway stops:** the gateway-side veth goes away with its namespace. Once no clients remain *and* the gateway is gone, the idle bridge is reaped.

---

## Part 10: Gateway mode flags and configuration

### Required vs optional flags

With `--net=gateway`, **only one flag is required**:

```bash
--gateway=<container_name>
```

Without it, Droidspaces prints an error and refuses to start. Everything else has a working default:

| Flag | Default | What it controls |
|---|---|---|
| `--gateway=NAME` | *(none, required)* | Which running container is the router |
| `--gateway-net=NAME` | `lan` | The LAN segment name, see below |
| `--gateway-iface=IFACE` | `eth1` | Interface name inside the gateway container |
| `--gateway-bridge=BR` | `ds-{gateway-net}` | Override the host bridge name entirely |

The shortest valid command is:

```bash
droidspaces --name=client --net=gateway --gateway=openwrt start
```

It is the same as spelling out every default:

```bash
droidspaces --name=client --net=gateway --gateway=openwrt \
  --gateway-net=lan \
  --gateway-iface=eth1 \
  start
```

### What --gateway-net does

This flag controls two things, both derived from the same name.

**1. It names the host bridge.**

The bridge Droidspaces creates on the host is named `ds-{NAME}`:

```
--gateway-net=lan   ->  host bridge: ds-lan
--gateway-net=vpn   ->  host bridge: ds-vpn
--gateway-net=iot   ->  host bridge: ds-iot
```

**2. It identifies the segment, that is, which bridge clients land on.**

The veth names for the gateway's LAN side come from a hash of the string `{gateway_container}:{gateway_net}`. Same hash, same veth, same bridge segment. So client containers that share the same `--gateway` and `--gateway-net` all end up on the same bridge and all get DHCP from the same OpenWRT interface.

This is what `--gateway-net` is for: running several isolated LAN segments through one gateway container.

```bash
# These two land on ds-lan - they see each other, OpenWRT routes them as one LAN
droidspaces --name=kali   --net=gateway --gateway=openwrt --gateway-net=lan start
droidspaces --name=ubuntu --net=gateway --gateway=openwrt --gateway-net=lan start

# This one lands on ds-vpn - a completely separate bridge
# OpenWRT can apply different firewall/VPN rules to this segment
droidspaces --name=torbox --net=gateway --gateway=openwrt --gateway-net=vpn start
```

Inside OpenWRT, the `lan` clients arrive on `eth1` and the `vpn` clients on `eth2`. Each segment gets its own veth, because `openwrt:lan` and `openwrt:vpn` hash differently.

### What --gateway-iface does

This sets **the name of the LAN interface inside the gateway container's network namespace**.

When Droidspaces creates the gateway veth for a segment, the gateway's end is born directly inside OpenWRT's netns under the name you pass here (default `eth1`); only the host end carries a hash name (`ds-gXXXXXXXX`).

**Why it matters:** OpenWRT's configuration is keyed on interface names. If your OpenWRT `/etc/config/network` says:

```
config interface 'lan'
    option device 'eth1'
```

then the interface that appears inside OpenWRT **must** be named `eth1`, or OpenWRT will not treat it as its LAN and will not serve DHCP on it. `--gateway-iface=eth1` takes care of that.

For a second segment, pass `--gateway-iface=eth2` so OpenWRT sees a separate interface and you can add a second UCI network block for it.

**Important detail:** `--gateway-iface` only takes effect when the gateway veth for a segment is first created, which is when the first client container on that segment starts. The gateway veth is shared by every client on the same `--gateway-net`: it is created once and reused. Every later client skips creating the gateway veth and only wires its own app veth into the existing bridge.

So if you start two containers on `--gateway-net=lan` and both pass `--gateway-iface=eth1`, that works: the first creates the veth and renames it `eth1`, and the second finds the veth already there and ignores `--gateway-iface`.

### The flag conflict you must avoid

The problem only appears when you use **two different `--gateway-net` segments with the same `--gateway-iface`**:

```bash
# segment 1 - creates eth1 inside OpenWRT
droidspaces --name=kali   --net=gateway --gateway=openwrt --gateway-net=lan --gateway-iface=eth1 start

# segment 2 - WRONG: also tries to create eth1 inside OpenWRT
droidspaces --name=torbox --net=gateway --gateway=openwrt --gateway-net=vpn --gateway-iface=eth1 start
```

When the second command runs, Droidspaces tries to move a new veth peer into OpenWRT and rename it `eth1`, but `eth1` already exists there from the first segment. Instead of failing loudly, the code detects the conflict and brings the existing `eth1` up again, leaving the new veth peer inside OpenWRT under its raw hash name (`ds-hYYYYYYYY`). OpenWRT has no config for `ds-hYYYYYYYY` and silently ignores it. The `vpn` segment gets no gateway-side interface: no DHCP, no routing, and its containers are effectively isolated.

**The rule:** every `--gateway-net` segment needs its own `--gateway-iface` name.

```bash
# Correct: two segments, two interface names
--gateway-net=lan  --gateway-iface=eth1   ->  eth1 inside OpenWRT (LAN segment)
--gateway-net=vpn  --gateway-iface=eth2   ->  eth2 inside OpenWRT (VPN segment)
```

### IPv6 through the gateway

NAT mode gives a container IPv6 as well as IPv4: Droidspaces announces a private IPv6 prefix (`fd64:7370::/64`) with router advertisements and translates it on the way out, the same idea as IPv4 NAT. The gateway container's WAN is a NAT interface, so OpenWRT can have IPv6 too. It does not pick it up by itself, and there are two things to understand before the steps:

- **Droidspaces hands OpenWRT one address, not a block of addresses to pass on.** OpenWRT therefore numbers its own LAN from a private prefix of its own (a ULA) and does IPv6 NAT a second time on its WAN. Client traffic is translated twice, once by OpenWRT and once by Droidspaces. It works, it is just not how IPv6 is used on a real ISP line.
- **The host kernel does the work for both.** A container shares the host's kernel, so both Droidspaces and OpenWRT need `CONFIG_IP6_NF_NAT` and `CONFIG_IP6_NF_TARGET_MASQUERADE` in it. `droidspaces check` reports this as "IPv6 NAT support".

The OpenWRT image from the Droidspaces rootfs repository already ships steps 1, 2 and 4. With it you only need step 3 for each LAN. On any other OpenWRT image, do all of them, inside the gateway container.

**1. Add an IPv6 WAN on `eth0`**

```bash
uci set network.wan6=interface
uci set network.wan6.device='eth0'
uci set network.wan6.proto='dhcpv6'
uci set network.wan6.sourcefilter='0'
```

`sourcefilter '0'` matters. By default OpenWRT installs its IPv6 default route as "only for packets from my WAN prefix". Two things break with that: Android kernels are built without `CONFIG_IPV6_SUBTREES`, so the route silently fails to install, and LAN clients carry a different source address anyway, so the route would not apply to them.

**2. Make sure OpenWRT has a private prefix of its own**

```bash
uci get network.globals.ula_prefix
```

If that prints a prefix starting with `fd`, you are done. If it says "Entry not found", set one. Any `fd` prefix with 10 random hex digits is fine:

```bash
uci set network.globals=globals
uci set network.globals.ula_prefix='fd3a:91c2:7b4e::/48'
```

**3. Turn IPv6 on for each LAN**

Replace `lan` with the name of your interface. Do this once for every LAN segment you want IPv6 on.

```bash
uci set network.lan.ip6assign='64'
uci set dhcp.lan.ra='server'
uci set dhcp.lan.dhcpv6='server'
uci set dhcp.lan.ra_default='1'
```

`ip6assign` gives the interface a `/64` out of the private prefix. `ra` and `dhcpv6` make OpenWRT announce it to clients. `ra_default '1'` tells clients to use OpenWRT as their IPv6 gateway even though the prefix is a private one, which OpenWRT would otherwise refuse to do.

**4. Firewall: let IPv6 in, and translate it on the way out**

Put `wan6` in the same zone as `wan`, so the same forwarding rules cover it. If your zones reject input, also allow ICMPv6 and DHCPv6, because IPv6 cannot find its neighbours or its router without them.

On an image with the iptables firewall (fw3, which is what Android needs), IPv6 NAT goes in through a small script:

```bash
cat > /etc/firewall.nat6 <<'EOF'
ip6tables -t nat -C POSTROUTING -o eth0 -j MASQUERADE 2>/dev/null ||
	ip6tables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
EOF
uci set firewall.nat6=include
uci set firewall.nat6.path='/etc/firewall.nat6'
uci set firewall.nat6.reload='1'
```

On an image with the nftables firewall (fw4, the OpenWRT default on a Linux host), it is one option on the `wan` zone instead: `option masq6 '1'`.

**5. Apply and check**

```bash
uci commit
/etc/init.d/network reload
/etc/init.d/firewall restart
/etc/init.d/odhcpd restart
```

Give it about twenty seconds, then:

```bash
ip -6 addr show eth0        # an fd64:7370:: address
ip -6 route | grep default  # default via fe80::1 dev eth0
ping6 -c3 google.com
```

A client container on the LAN then gets an address from OpenWRT's private prefix and can `ping6` the internet.

Three things to expect:

- **Clients still prefer IPv4** for sites that have both. Operating systems rank a private IPv6 source below IPv4, so IPv6 is used for destinations that are IPv6 only.
- **Mirror your isolation rules.** If your firewall blocks clients from reaching the Droidspaces NAT network `172.28.0.0/16`, add the same rule for `fd64:7370::/48` with `option family 'ipv6'`, or clients can reach other NAT containers over IPv6.
- **`--disable-ipv6` on the gateway container turns all of this off**, and on a client container it turns IPv6 off for that client only.

### Validation rules and kernel requirements

Droidspaces checks a few rules at startup and refuses to boot if any is broken:

- A container cannot be its own gateway (`--gateway` must name a different container).
- Interface and bridge names must be shorter than 16 characters (the Linux `IFNAMSIZ` limit) and may contain only letters, digits, `_` and `-`.
- The kernel must support network namespaces (`CONFIG_NET_NS`), veth pairs (`CONFIG_VETH`) and bridges (`CONFIG_BRIDGE`). Droidspaces probes for all three before starting and exits with a fatal error if any is missing.

Two more things to know:

- `--port` only makes sense in NAT mode. In gateway mode it is ignored with a warning, because port forwarding and uplink selection are the gateway container's job.
- When the host bridge name is derived from `--gateway-net`, the name is sanitised (only letters, digits, `_` and `-` are kept) and truncated to 9 characters, giving `ds-` plus at most 9 characters. If you need an exact bridge name, set it with `--gateway-bridge`.

---

## Part 11: Comparing all networking modes

| Feature | NAT Mode | Host Mode | None Mode | Gateway Mode |
|---|---|---|---|---|
| Who assigns IPs? | Droidspaces DHCP | Android (shared) | Nobody (loopback only) | OpenWRT dnsmasq |
| Who does NAT? | Droidspaces iptables | Android | N/A | OpenWRT (via Droidspaces NAT on OpenWRT's WAN) |
| Who manages firewall? | Droidspaces | Android | N/A | OpenWRT |
| Who manages DNS? | Droidspaces | Android | Nobody | OpenWRT dnsmasq |
| Container isolated from host network? | Yes | No | Yes | Yes |
| Internet access? | Yes | Yes | No | Yes (via gateway container) |
| IPv6? | Yes, with NAT66 (needs kernel support) | Whatever Android has | Loopback only | Yes, once OpenWRT is set up for it (see Part 10) |
| Needs a second container to function? | No | No | No | Yes (the gateway container) |
| Good for | Simple internet access | Maximum performance, no veth or bridge in the path | Offline / sandboxed workloads | Router appliance, VPN gateway, segmented LANs |

---

## Part 12: Real-world use cases for gateway mode

### 1. VPN killswitch for specific containers

Run OpenWRT with a WireGuard or OpenVPN client, and configure its firewall to drop all traffic that does not go through the VPN tunnel. No gateway-mode container can then leak traffic outside the VPN. OpenWRT enforces it at the bridge, not inside each container.

### 2. Multiple isolated LAN segments

Use `--gateway-net` to create separate segments on the same OpenWRT. Containers on `--gateway-net=lan` cannot reach containers on `--gateway-net=vpn` unless OpenWRT routes between them. You get VLAN-style isolation from a single gateway container.

### 3. Traffic analysis

Run OpenWRT with `tcpdump` or `nftables` logging enabled. Every packet from every gateway-mode container passes through OpenWRT, so you have one chokepoint from which to watch the network activity of many containers at once.

### 4. Custom DNS filtering

Run OpenWRT with a `dnsmasq` blocklist (or with Adblock installed via opkg). Every container on the gateway LAN gets filtered DNS without configuring each container.

### 5. Bandwidth shaping

OpenWRT's `tc` (traffic control) and `sqm-scripts` can shape bandwidth per container, because OpenWRT sees each container as a separate MAC address on its LAN interface.

---

## Quick reference - terms

| Term | One-line definition |
|---|---|
| **IP address** | The numerical address of a device on a network (e.g. `192.168.1.5`) |
| **MAC address** | The hardware address of a network interface, used for delivery within the same network |
| **LAN** | Local network - devices near each other that can talk directly |
| **WAN** | Wide network - the internet, outside your local network |
| **Gateway** | A device that connects two networks and routes traffic between them |
| **DHCP** | Protocol for automatically assigning IP addresses to devices |
| **DNS** | System that converts human-readable names (`google.com`) to IP addresses |
| **NAT** | Technique for sharing one public IP across many private-IP devices |
| **Bridge** | A virtual (or physical) switch that connects multiple network interfaces |
| **veth pair** | A pair of virtual network interfaces connected like a pipe - what goes in one end comes out the other |
| **Network namespace** | An isolated copy of the Linux networking stack - containers live in their own namespace |
| **OpenWRT** | A Linux distro designed to run as a router/gateway - runs dnsmasq, netifd, firewall |
| **netifd** | OpenWRT's network interface daemon - manages interfaces and DHCP |
| **dnsmasq** | Lightweight DHCP and DNS server used by OpenWRT |
| **MASQUERADE** | The Linux iptables rule that implements NAT (rewrites source IPs) |
| **NAT66** | NAT for IPv6: private IPv6 addresses rewritten to the uplink's address on the way out |
| **ULA** | A private IPv6 prefix starting with `fd`, the IPv6 counterpart of `192.168.x.x` |
| **Router advertisement** | The message an IPv6 router sends so devices can configure their own address and gateway, IPv6's replacement for most of DHCP |
| **Delegated LAN** | The bridge network Droidspaces creates in gateway mode - policy owned by the gateway container, not Droidspaces |
| **Segment** | One isolated LAN identified by `--gateway-net` - each segment gets its own bridge and its own interface inside the gateway container |
| **Lazy attachment** | The gateway's LAN-side veth is only created when the first client container starts, not when the gateway container starts |
