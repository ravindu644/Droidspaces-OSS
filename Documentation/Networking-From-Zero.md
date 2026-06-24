<!--
title: Networking From Zero
section: Guides
order: 5
desc: A beginner-friendly guide to every networking concept behind Droidspaces gateway mode - IP addresses, LAN, WAN, DHCP, DNS, NAT, bridges, veth pairs, network namespaces, and OpenWRT.
keywords: droidspaces, networking, gateway, openwrt, nat, dhcp, dns, lan, wan, veth, bridge, namespace, linux, android
-->

# Networking From Zero: Understanding Droidspaces Gateway Mode

### Quick Navigation

- [Part 1: The Absolute Basics](#part-1-the-absolute-basics)
    - [What is an IP address?](#what-is-an-ip-address)
    - [What is a network?](#what-is-a-network)
    - [LAN - Local Area Network](#lan---local-area-network)
    - [WAN - Wide Area Network](#wan---wide-area-network)
    - [Gateway](#gateway)
- [Part 2: How Data Actually Gets Delivered](#part-2-how-data-actually-gets-delivered)
    - [MAC address vs IP address](#mac-address-vs-ip-address)
    - [What is a packet?](#what-is-a-packet)
- [Part 3: DHCP (How You Get an IP Address)](#part-3-dhcp-how-you-get-an-ip-address)
- [Part 4: DNS (How Names Become Addresses)](#part-4-dns-how-names-become-addresses)
- [Part 5: NAT (The Magic Your Router Does)](#part-5-nat-the-magic-your-router-does)
    - [NAT in Droidspaces](#nat-in-droidspaces)
- [Part 6: Bridges and Virtual Cables (Linux Plumbing)](#part-6-bridges-and-virtual-cables-linux-plumbing)
    - [What is a network bridge?](#what-is-a-network-bridge)
    - [What is a veth pair?](#what-is-a-veth-pair)
    - [How NAT mode uses bridges and veths](#how-nat-mode-uses-bridges-and-veths)
- [Part 7: Network Namespaces (How Containers Are Isolated)](#part-7-network-namespaces-how-containers-are-isolated)
- [Part 8: OpenWRT and What It Is](#part-8-openwrt-and-what-it-is)
- [Part 9: The New Gateway Mode - Putting It All Together](#part-9-the-new-gateway-mode---putting-it-all-together)
    - [Why does gateway mode exist?](#why-does-gateway-mode-exist)
    - [The architecture](#the-architecture)
    - [Step by step - what happens when you start a gateway-mode container](#step-by-step---what-happens-when-you-start-a-gateway-mode-container)
    - [What "lazy attachment" means](#what-lazy-attachment-means)
    - [Why resolv.conf is left alone in gateway mode](#why-resolvconf-is-left-alone-in-gateway-mode)
    - [Why bridge-nf-call-iptables is set to 0](#why-bridge-nf-call-iptables-is-set-to-0)
    - [The gateway container must be running first](#the-gateway-container-must-be-running-first)
    - [What happens when containers stop](#what-happens-when-containers-stop)
- [Part 10: Gateway Mode Flags and Configuration](#part-10-gateway-mode-flags-and-configuration)
    - [Required vs optional flags](#required-vs-optional-flags)
    - [What --gateway-net does](#what---gateway-net-does)
    - [What --gateway-iface does](#what---gateway-iface-does)
    - [The flag conflict you must avoid](#the-flag-conflict-you-must-avoid)
    - [Validation rules and kernel requirements](#validation-rules-and-kernel-requirements)
- [Part 11: Comparing All Networking Modes](#part-11-comparing-all-networking-modes)
- [Part 12: Real-World Use Cases for Gateway Mode](#part-12-real-world-use-cases-for-gateway-mode)
- [Quick Reference - Terms](#quick-reference---terms)

---

## Part 1: The Absolute Basics

### What is an IP address?

Every device that wants to talk over a network needs an address so other devices know where to send data. That address is called an **IP address**.

Think of it like a house address. If someone wants to mail you a letter, they need your address. Same idea: if your phone wants to send data to Google, it needs to know Google's address, and Google needs to know your phone's address to send the reply back.

An IP address looks like this: `192.168.1.5`

It is four numbers (0-255) separated by dots. Each number is called an **octet**.

### What is a network?

A **network** is just a group of devices that can talk to each other directly.

Imagine a room with 5 laptops all connected to the same Wi-Fi router. Those 5 laptops are on the same network. They can send files to each other without going through the internet.

### LAN - Local Area Network

**LAN** = the network *inside your home* (or office, or in our case, *inside the container world*).

It is called "local" because it is physically nearby: your phone, laptop, smart TV, all connected to your home Wi-Fi router. They all live on the same LAN. They can talk to each other directly.

LAN addresses usually look like:
- `192.168.x.x`
- `10.x.x.x`
- `172.16.x.x` to `172.31.x.x`

These are called **private IP ranges**. They are reserved for local networks and are never used on the public internet.

### WAN - Wide Area Network

**WAN** = the network *outside your home*: the internet itself.

Your router has two sides:
- The **LAN side** faces your devices at home
- The **WAN side** faces your internet provider (ISP)

Your ISP gives your router one public IP address for the WAN side. Everything inside your home shares that one public IP to reach the internet.

```
[Your Phone]--+
[Your Laptop]-+--[Router]--[ISP]--[The Internet]
[Your TV]-----+
  (LAN side)     (WAN side)
```

### Gateway

A **gateway** is the device that connects two different networks together.

In your home, the router *is* the gateway. Your phone's IP is `192.168.1.5` (LAN). When your phone wants to reach Google at `142.250.80.46` (WAN/internet), it does not know how to get there directly. So it sends the data to the gateway (router), and the router figures out how to forward it to the internet.

**Rule:** Every device on a LAN is configured with a "default gateway" - the address they send all traffic to when they do not know where else to send it.

---

## Part 2: How Data Actually Gets Delivered

### MAC address vs IP address

There are actually *two* kinds of addresses in networking:

| Type | Looks like | Purpose |
|---|---|---|
| **IP address** | `192.168.1.5` | Logical address - used for routing across networks |
| **MAC address** | `a4:c3:f0:12:34:56` | Physical address - used for delivery on the *same* network |

Think of it this way:
- The IP address is the **city and street** - used to navigate across the country
- The MAC address is the **apartment number** - used once you arrive at the building

When your laptop sends a packet to your router, it uses the router's MAC address (because they are on the same LAN). The router then uses IP addresses to figure out where to send it next.

### What is a packet?

Data traveling over a network is broken into small chunks called **packets**. Each packet contains:
- Where it came from (source IP)
- Where it is going (destination IP)
- A small piece of the actual data

The network reassembles all the packets at the destination.

---

## Part 3: DHCP (How You Get an IP Address)

### The problem

Every device needs an IP address to join a network. But you cannot have two devices with the same IP - that would be like two houses with the same postal address. Mail would get lost.

You *could* manually assign a unique IP to every device, but that is painful. What if you have 50 devices?

### The solution: DHCP

**DHCP** = Dynamic Host Configuration Protocol

It is a system where one device (the **DHCP server**) automatically hands out IP addresses to every new device that joins the network.

The conversation goes like this:

```
New Device:   "Hello? Anyone there? I just joined this network and I need an IP address."
DHCP Server:  "I heard you. Here, take 192.168.1.42. Also, your gateway is 192.168.1.1,
               and for DNS use 1.1.1.1. Your lease lasts 24 hours."
New Device:   "Got it, thanks!"
```

The new device now has everything it needs to work on the network:
- Its own IP address
- The gateway address (so it knows where to send traffic)
- The DNS address (explained next)

In your home, the **router runs the DHCP server**. It hands out IPs to every device that connects.

In **Droidspaces NAT mode**, Droidspaces itself runs a mini DHCP server that hands the container its IP (in the `172.28.x.x` range). The IP is deterministic: it is derived from the container's name, persisted to its config file, and offered again on every boot, so the same container always keeps the same address across restarts.

---

## Part 4: DNS (How Names Become Addresses)

### The problem

IP addresses are hard to remember. Nobody types `142.250.80.46` when they want to visit Google. They type `google.com`.

But computers only understand IP addresses. So there needs to be a system that converts human-readable names to IP addresses.

### The solution: DNS

**DNS** = Domain Name System

It is basically the internet's phone book. You give it a name (`google.com`), it gives you back an IP address (`142.250.80.46`).

The conversation:
```
Your Browser:   "What is the IP address of google.com?"
DNS Server:     "It is 142.250.80.46"
Your Browser:   "Thanks." [now connects to 142.250.80.46]
```

Every device is configured with a DNS server address. In most home networks, the router *is* the DNS server (it forwards your questions to your ISP's DNS or a public one like `1.1.1.1`).

In **Droidspaces NAT mode**, Droidspaces writes a `resolv.conf` file inside the container pointing to a DNS server (default `1.1.1.1` and `8.8.8.8`, or whatever you pass via `--dns`). The same DNS servers are also advertised inside the DHCP lease itself.

---

## Part 5: NAT (The Magic Your Router Does)

### The problem

Your ISP gives you *one* public IP address. But you have 10 devices at home. How do all 10 devices use the internet at the same time?

### The solution: NAT

**NAT** = Network Address Translation

Your router keeps a secret table. When a device inside your LAN sends a packet to the internet, the router:
1. Rewrites the source IP from the device's private IP (`192.168.1.5`) to the router's public IP
2. Remembers which device sent it
3. When the reply comes back from the internet, rewrites the destination back to the device's private IP and forwards it

From the internet's perspective, all your home devices appear to be *one device*: the router.

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

In Droidspaces NAT mode, Droidspaces acts *exactly like your home router* - but for containers:

- The container gets a private IP (`172.28.x.x`)
- Droidspaces installs iptables `MASQUERADE` rules (that is the Linux name for the NAT target), plus FORWARD-accept and MSS-clamp rules so traffic actually flows
- The container can reach the internet; the internet sees Android's IP, not the container's IP
- Droidspaces also runs an embedded DHCP server for the container and configures its DNS
- On Android, a background route monitor detects the active internet uplink automatically (by reading the kernel's routing rules) and re-points container traffic the moment the active network changes (for example, Wi-Fi to mobile data handoff)

---

## Part 6: Bridges and Virtual Cables (Linux Plumbing)

Now we go one level deeper, into how Linux actually connects containers to each other.

### What is a network bridge?

A **network bridge** works like a network switch. A physical network switch is a box you plug multiple ethernet cables into; all devices connected to it can talk to each other.

A Linux **bridge** is a virtual switch, entirely in software. You can create it with a command, and then "plug" virtual network interfaces into it.

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

A veth pair is a pair of virtual network interfaces that are connected to each other like a pipe. Anything you send into one end comes out the other end.

Think of it as a virtual ethernet cable with two plugs. You put one plug inside a container, and the other plug stays on the host (or goes into a bridge).

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

The bridge `ds-br0` owns the gateway IP `172.28.0.1`, which every NAT container uses as its default gateway. The veth pair is named after the container's init process ID: the host side is `ds-v<PID>`, and the container side starts life as `ds-p<PID>` before being renamed to `eth0` inside the container.

Droidspaces runs a small per-container DHCP server listening on the container's host-side veth. The whole `172.28.0.0/16` subnet belongs to Droidspaces (the `172.28.0.x` row is reserved for the gateway itself, so containers always land in `172.28.1.x` through `172.28.254.x`), and everything from it is NATted out through Android's real interface.

---

## Part 7: Network Namespaces (How Containers Are Isolated)

### What is a namespace?

Linux has a feature called **namespaces** that lets you create isolated views of system resources.

A **network namespace** is an isolated copy of the entire networking stack. It has its own:
- Network interfaces
- Routing table
- iptables rules
- Everything networking-related

When Droidspaces starts a container, it creates a new network namespace for it. The container lives in that namespace. It cannot see the host's network interfaces at all - only what Droidspaces explicitly puts inside its namespace.

The veth pair is the tunnel between the host's namespace and the container's namespace:
- One end of the veth goes inside the container's network namespace (appears as `eth0`)
- The other end stays in the host's network namespace (Droidspaces connects it to a bridge)

---

## Part 8: OpenWRT and What It Is

### What is OpenWRT?

**OpenWRT** is a Linux distribution designed specifically for routers. Normally it runs on physical router hardware, but it can also run on a regular Linux system or inside a container.

When OpenWRT is running, it provides:
- **netifd** - network interface daemon (manages network interfaces, DHCP client/server, etc.)
- **dnsmasq** - DNS and DHCP server
- **firewall3** or **nftables** - firewall
- **LuCI** - web UI for configuration
- Everything a real router does, in software

This means you can run OpenWRT inside a Droidspaces container, and it will behave exactly like a real router: managing networks, handing out DHCP leases, doing DNS, applying firewall rules, routing VPN traffic, etc.

---

## Part 9: The New Gateway Mode - Putting It All Together

### Why does gateway mode exist?

In NAT mode, Droidspaces is the router. It does everything. This is fine for most cases.

But what if you want **OpenWRT to be the router** for other containers? You want OpenWRT's firewall rules, OpenWRT's DHCP, OpenWRT's VPN routing, and you want other containers (like a Kali Linux container) to be on OpenWRT's LAN, getting everything from OpenWRT.

The problem: if Droidspaces also tries to install NAT, DHCP, and DNS for those containers, it will *conflict* with what OpenWRT is trying to do. Two DHCP servers fighting over who gives the IP address. Two firewalls applying contradictory rules.

**Gateway mode solves this.** Droidspaces steps back. It only does the L2 plumbing (the virtual cables and switch), and lets OpenWRT own all the policy: DHCP, DNS, firewall, routing.

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
|    +-- ds-v[pid]  (veth host-side, connected to Kali's netns as eth0)
|
+-- [Kali container - net=gateway mode]
     netns: owns eth0 (LAN side - plugged into ds-lan bridge)
     Gets DHCP from OpenWRT's dnsmasq
     Routing decisions made by OpenWRT
     Firewall rules applied by OpenWRT
```

### Step by step - what happens when you start a gateway-mode container

**Step 1 - You start OpenWRT first (in NAT mode)**

```bash
droidspaces --name=openwrt --rootfs=/data/openwrt --net=nat start
```

OpenWRT boots. It has:
- `eth0` on the WAN side (Droidspaces manages NAT for this)
- No LAN side yet - OpenWRT is waiting for one

**Step 2 - You start Kali (in gateway mode)**

```bash
droidspaces --name=kali --rootfs=/data/kali --net=gateway --gateway=openwrt start
```

Droidspaces does the following (only plumbing, no policy):

1. Finds OpenWRT's running process ID so it can reach its network namespace
2. Creates a bridge called `ds-lan` on the host with no IP address on it
3. Disables `bridge-nf-call-iptables` so Android's host firewall does NOT intercept traffic on this bridge, keeping OpenWRT's firewall as the only authority
4. Creates a veth pair for OpenWRT's LAN side - one end goes into OpenWRT's netns (appears as `eth1`), the other end plugs into the `ds-lan` bridge
5. Creates a veth pair for Kali - one end goes into Kali's netns (appears as `eth0`), the other end plugs into the `ds-lan` bridge
6. Does NOT install NAT, DHCP, DNS, or any firewall rules

**Step 3 - OpenWRT takes over**

OpenWRT's `netifd` detects that `eth1` appeared. It configures it as the LAN interface.
OpenWRT's `dnsmasq` starts answering DHCP requests on `eth1`.

Kali's `eth0` sends a DHCP request and OpenWRT's `dnsmasq` replies with:
- IP address: `192.168.1.100` (or whatever OpenWRT's DHCP range is)
- Gateway: `192.168.1.1` (OpenWRT itself)
- DNS: `192.168.1.1` (OpenWRT's dnsmasq)

Kali is now fully configured, with OpenWRT as its router.

**Step 4 - Traffic flows through OpenWRT**

When Kali tries to reach the internet:

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

OpenWRT's firewall sees all of Kali's traffic and can apply any rules: block certain sites, redirect through VPN, shape bandwidth, log connections - exactly like a real router would.

### What "lazy attachment" means

The gateway veth is "lazily attached." This means:

- When you start OpenWRT, it does NOT immediately get an `eth1`
- `eth1` only appears inside OpenWRT **when the first gateway-mode container starts**
- This is intentional - OpenWRT boots with just its WAN side (`eth0`), and its LAN cable (`eth1`) is plugged in later, on demand

This mimics how you might physically plug a cable into a router's LAN port after the router is already running.

### Why resolv.conf is left alone in gateway mode

In NAT mode, Droidspaces writes `/etc/resolv.conf` inside the container, pointing to `1.1.1.1` or `8.8.8.8`.

In gateway mode, Droidspaces does NOT write `resolv.conf` (unless you explicitly pass `--dns`). This is because OpenWRT's `dnsmasq` hands the DNS server address to the container via the DHCP lease. If Droidspaces also wrote a `resolv.conf`, it would conflict with what dnsmasq is providing - the container would use the wrong DNS and bypass OpenWRT's DNS filtering/caching entirely.

### Why bridge-nf-call-iptables is set to 0

The bridge `ds-lan` carries traffic between OpenWRT and Kali. By default, Linux can pass bridged traffic through the host's iptables. This would mean Android's iptables rules (which might drop or NAT things unexpectedly) would interfere with traffic that OpenWRT is supposed to be managing.

Setting it to `0` tells Linux: "do not run iptables on bridged traffic." This keeps OpenWRT's firewall as the *only* firewall that sees this traffic, which is exactly what we want.

### The gateway container must be running first

Start order matters. When a gateway-mode container starts, Droidspaces looks up the gateway container's live process ID to reach its network namespace. If the gateway container is not running at that moment, the network setup fails with a warning and the client container boots anyway - but with no network at all (only loopback). It does not retry on its own.

The same logic applies after a gateway restart. A veth pair dies together: when the gateway container stops, the `eth1` end inside it is destroyed, and that destroys the host-side end too. Existing clients stay plugged into the bridge but have no router anymore. The LAN cable is re-plugged the next time **any** gateway-mode container starts on that segment - so after restarting the gateway, restart one client (or start a new one) to bring the segment back to life.

### What happens when containers stop

Cleanup in gateway mode is deliberately minimal, matching the "plumbing only" philosophy:

- **A client stops:** only that client's own veth (`ds-v<PID>`) is removed. The bridge and the gateway's `eth1` stay up, so other clients on the segment are untouched.
- **The gateway stops:** the gateway-side veth disappears with its namespace (see above), but the bridge itself stays.
- The delegated bridge (`ds-lan` etc.) is never torn down by Droidspaces. It is harmless when idle - it has no IP and carries no policy - and persists until you delete it manually or reboot the device.

---

## Part 10: Gateway Mode Flags and Configuration

### Required vs optional flags

Only **one flag is mandatory** when using `--net=gateway`:

```bash
--gateway=<container_name>
```

If you omit it, Droidspaces will print an error and refuse to start. Everything else has a working default:

| Flag | Default | What it controls |
|---|---|---|
| `--gateway=NAME` | *(none - required)* | Which running container is the router |
| `--gateway-net=NAME` | `lan` | The LAN segment name - see below |
| `--gateway-iface=IFACE` | `eth1` | Interface name inside the gateway container |
| `--gateway-bridge=BR` | `ds-{gateway-net}` | Override the host bridge name entirely |

So the minimal valid command is:

```bash
droidspaces --name=client --net=gateway --gateway=openwrt start
```

That is identical to spelling out all defaults explicitly:

```bash
droidspaces --name=client --net=gateway --gateway=openwrt \
  --gateway-net=lan \
  --gateway-iface=eth1 \
  start
```

### What --gateway-net does

This flag controls two things at once, both derived from the same name.

**1. It names the host bridge.**

The bridge Droidspaces creates on the host is named `ds-{NAME}`:

```
--gateway-net=lan   ->  host bridge: ds-lan
--gateway-net=vpn   ->  host bridge: ds-vpn
--gateway-net=iot   ->  host bridge: ds-iot
```

**2. It is the segment identifier - which bridge clients land on.**

The veth names for the gateway's LAN side are generated by hashing the string `{gateway_container}:{gateway_net}` together. The same hash = the same veth = the same bridge segment. This means multiple client containers that share the same `--gateway` and `--gateway-net` all end up on the same bridge, and all get DHCP from the same OpenWRT interface.

This is the real power of `--gateway-net`: running multiple isolated LAN segments through the same gateway container.

```bash
# These two land on ds-lan - they see each other, OpenWRT routes them as one LAN
droidspaces --name=kali   --net=gateway --gateway=openwrt --gateway-net=lan start
droidspaces --name=ubuntu --net=gateway --gateway=openwrt --gateway-net=lan start

# This one lands on ds-vpn - a completely separate bridge
# OpenWRT can apply different firewall/VPN rules to this segment
droidspaces --name=torbox --net=gateway --gateway=openwrt --gateway-net=vpn start
```

Inside OpenWRT, the `lan` clients come in on `eth1` and the `vpn` clients come in on `eth2` (each segment gets its own veth, because the hash of `openwrt:lan` and `openwrt:vpn` are different).

### What --gateway-iface does

This controls **what the LAN interface is called inside the gateway container's network namespace**.

When Droidspaces creates the gateway veth for a segment, it moves one end into OpenWRT's netns and renames it from its raw hash name (`ds-hXXXXXXXX`) to whatever you pass here (default `eth1`).

**Why does this matter?** OpenWRT's configuration is built around interface names. If your OpenWRT `/etc/config/network` says:

```
config interface 'lan'
    option device 'eth1'
```

...then the interface that appears inside OpenWRT **must** be named `eth1`, or OpenWRT will not recognize it as its LAN and will not serve DHCP on it. `--gateway-iface=eth1` makes that happen.

For a second segment you would pass `--gateway-iface=eth2` so OpenWRT sees it as a separate interface and you can add a second UCI network block for it.

**Important detail:** `--gateway-iface` only has any effect when the gateway veth for that segment is being created for the first time - which is when the very first client container on that segment starts. The gateway veth is shared by all clients on the same `--gateway-net`; it is created once and reused. Every client after the first one skips the gateway veth creation entirely and just wires its own app veth into the existing bridge.

This means if you start two containers on `--gateway-net=lan` and both pass `--gateway-iface=eth1`, it works perfectly fine: the first container creates the veth and renames it `eth1`, the second container sees the veth already exists and does nothing with `--gateway-iface` at all.

### The flag conflict you must avoid

The problem only happens when you use **two different `--gateway-net` segments but the same `--gateway-iface`**:

```bash
# segment 1 - creates eth1 inside OpenWRT
droidspaces --name=kali   --net=gateway --gateway=openwrt --gateway-net=lan --gateway-iface=eth1 start

# segment 2 - WRONG: also tries to create eth1 inside OpenWRT
droidspaces --name=torbox --net=gateway --gateway=openwrt --gateway-net=vpn --gateway-iface=eth1 start
```

When the second command runs, Droidspaces tries to move a new veth peer into OpenWRT and rename it `eth1`. But `eth1` already exists inside OpenWRT from the first segment. Instead of failing loudly, the code detects the conflict and just brings the existing `eth1` up again, leaving the new veth peer with its raw hash name (`ds-hYYYYYYYY`) inside OpenWRT. OpenWRT has no config for `ds-hYYYYYYYY` and silently ignores it. The `vpn` segment gets no gateway-side interface: no DHCP, no routing, containers on it are effectively isolated.

**The rule:** every `--gateway-net` segment must have a unique `--gateway-iface` name.

```bash
# Correct: two segments, two interface names
--gateway-net=lan  --gateway-iface=eth1   ->  eth1 inside OpenWRT (LAN segment)
--gateway-net=vpn  --gateway-iface=eth2   ->  eth2 inside OpenWRT (VPN segment)
```

### Validation rules and kernel requirements

Droidspaces enforces a few rules at startup and refuses to boot if they are violated:

- A container cannot use **itself** as its gateway (`--gateway` must name a different container)
- Interface and bridge names must be shorter than 16 characters (the Linux `IFNAMSIZ` limit) and may only contain letters, digits, `_`, and `-`
- The kernel must support network namespaces (`CONFIG_NET_NS`), veth pairs (`CONFIG_VETH`), and bridges (`CONFIG_BRIDGE`). Droidspaces probes for all three before starting and exits with a fatal error if any is missing

Two more things to know:

- `--port` only makes sense in NAT mode. In gateway mode it is ignored with a warning - port forwarding and uplink selection are the gateway container's job now.
- When the host bridge name is auto-derived from `--gateway-net`, the name is sanitized (only letters, digits, `_`, `-` survive) and truncated to 9 characters, producing `ds-` plus at most 9 characters. If you need an exact bridge name, set it explicitly with `--gateway-bridge`.

---

## Part 11: Comparing All Networking Modes

| Feature | NAT Mode | Host Mode | None Mode | Gateway Mode |
|---|---|---|---|---|
| Who assigns IPs? | Droidspaces DHCP | Android (shared) | Nobody (loopback only) | OpenWRT dnsmasq |
| Who does NAT? | Droidspaces iptables | Android | N/A | OpenWRT (via Droidspaces NAT on OpenWRT's WAN) |
| Who manages firewall? | Droidspaces | Android | N/A | OpenWRT |
| Who manages DNS? | Droidspaces | Android | Nobody | OpenWRT dnsmasq |
| Container isolated from host network? | Yes | No | Yes | Yes |
| Internet access? | Yes | Yes | No | Yes (via gateway container) |
| Needs a second container to function? | No | No | No | Yes (the gateway container) |
| Good for | Simple internet access | Maximum performance, zero overhead | Offline / sandboxed workloads | Router appliance, VPN gateway, segmented LANs |

---

## Part 12: Real-World Use Cases for Gateway Mode

### 1. VPN killswitch for specific containers

Run OpenWRT with a WireGuard or OpenVPN client. Configure OpenWRT's firewall to drop all traffic that does not go through the VPN tunnel. Any container using gateway mode will be unable to leak traffic outside the VPN - OpenWRT enforces it at the bridge level, not inside the individual containers.

### 2. Multiple isolated LAN segments

Use `--gateway-net` to create separate segments on the same OpenWRT. Containers on `--gateway-net=lan` cannot reach containers on `--gateway-net=vpn` unless OpenWRT explicitly routes between them. You get VLAN-style isolation with a single gateway container.

### 3. Traffic analysis

Run OpenWRT with `tcpdump` or `nftables` logging enabled. Every packet from every gateway-mode container flows through OpenWRT, so you get a single chokepoint to observe all network activity across multiple containers at once.

### 4. Custom DNS filtering

Run OpenWRT with a `dnsmasq` blocklist (or with Adblock installed via opkg). Every container on the gateway LAN gets filtered DNS without touching each container individually.

### 5. Bandwidth shaping

OpenWRT's `tc` (traffic control) and `sqm-scripts` can shape bandwidth per container, since OpenWRT sees each container as a separate MAC address arriving on its LAN interface.

---

## Quick Reference - Terms

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
| **Delegated LAN** | The bridge network Droidspaces creates in gateway mode - policy owned by the gateway container, not Droidspaces |
| **Segment** | One isolated LAN identified by `--gateway-net` - each segment gets its own bridge and its own interface inside the gateway container |
| **Lazy attachment** | The gateway's LAN-side veth is only created when the first client container starts, not when the gateway container starts |
