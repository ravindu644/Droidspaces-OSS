<!--
title: 从零开始的网络基础
section: 指南
order: 5
desc: 面向初学者的 Droidspaces 网关模式背后每个网络概念指南——IP 地址、LAN、WAN、DHCP、DNS、NAT、网桥、veth 对、网络命名空间与 OpenWRT。
keywords: droidspaces, networking, gateway, openwrt, nat, dhcp, dns, lan, wan, veth, bridge, namespace, linux, android
-->

# 从零开始的网络基础：理解 Droidspaces 网关模式

### 快速导航

- [第一部分：绝对基础](#第一部分绝对基础)
    - [什么是 IP 地址？](#什么是-ip-地址)
    - [什么是网络？](#什么是网络)
    - [LAN——局域网](#lan局域网)
    - [WAN——广域网](#wan广域网)
    - [网关](#网关)
- [第二部分：数据究竟是如何送达的](#第二部分数据究竟是如何送达的)
    - [MAC 地址 vs IP 地址](#mac-地址-vs-ip-地址)
    - [什么是数据包？](#什么是数据包)
- [第三部分：DHCP（你是如何获取 IP 地址的）](#第三部分dhcp你是如何获取-ip-地址的)
- [第四部分：DNS（域名如何变成地址）](#第四部分dns域名如何变成地址)
- [第五部分：NAT（你的路由器施展的魔法）](#第五部分nat你的路由器施展的魔法)
    - [NAT 在 Droidspaces 中的应用](#nat-在-droidspaces-中的应用)
- [第六部分：网桥与虚拟线缆（Linux 管道）](#第六部分网桥与虚拟线缆linux-管道)
    - [什么是网桥？](#什么是网桥)
    - [什么是 veth 对？](#什么是-veth-对)
    - [NAT 模式如何使用网桥和 veth](#nat-模式如何使用网桥和-veth)
- [第七部分：网络命名空间（容器是如何隔离的）](#第七部分网络命名空间容器是如何隔离的)
- [第八部分：OpenWRT 及其概念](#第八部分openwrt-及其概念)
- [第九部分：全新的网关模式——融会贯通](#第九部分全新的网关模式融会贯通)
    - [为什么要有网关模式？](#为什么要有网关模式)
    - [架构总览](#架构总览)
    - [逐步说明——启动网关模式容器时会发生什么](#逐步说明启动网关模式容器时会发生什么)
    - ["延迟挂接"的含义](#延迟挂接的含义)
    - [为什么网关模式下不修改 resolv.conf](#为什么网关模式下不修改-resolvconf)
    - [为什么 bridge-nf-call-iptables 要设为 0](#为什么-bridge-nf-call-iptables-要设为-0)
    - [网关容器必须先运行](#网关容器必须先运行)
    - [容器停止时会发生什么](#容器停止时会发生什么)
- [第十部分：网关模式的标志与配置](#第十部分网关模式的标志与配置)
    - [必选标志 vs 可选标志](#必选标志-vs-可选标志)
    - [--gateway-net 的作用](#--gateway-net-的作用)
    - [--gateway-iface 的作用](#--gateway-iface-的作用)
    - [必须避免的标志冲突](#必须避免的标志冲突)
    - [验证规则与内核要求](#验证规则与内核要求)
- [第十一部分：所有网络模式对比](#第十一部分所有网络模式对比)
- [第十二部分：网关模式的真实使用场景](#第十二部分网关模式的真实使用场景)
- [快速参考——术语表](#快速参考术语表)

---

## 第一部分：绝对基础

### 什么是 IP 地址？

任何想要在网络上通信的设备都需要一个地址，这样其他设备才知道将数据发往何处。这个地址称为 **IP 地址**。

可以把它想象成家庭住址。如果有人想给你寄信，就需要你的地址。同理：如果你的手机想给 Google 发送数据，就需要知道 Google 的地址，而 Google 也需要知道你手机的地址才能将回复发回来。

IP 地址看起来像这样：`192.168.1.5`

它是由点号分隔的四个数字（0-255）。每个数字称为一个 **八位组（octet）**。

### 什么是网络？

**网络**就是一组可以相互直接通信的设备。

想象一个房间里有 5 台笔记本电脑都连接着同一个 Wi-Fi 路由器。这 5 台笔记本在同一个网络上——它们可以彼此传输文件，而不需要经过互联网。

### LAN——局域网

**LAN** = 你**家庭内部**的网络（或者办公室内部，或者在我们的场景中，是**容器世界内部**）。

之所以叫"局域网"，是因为设备在物理上很近：你的手机、笔记本电脑、智能电视，都连接着你的家庭 Wi-Fi 路由器。它们都生活在同一个局域网内，可以直接相互通信。

局域网地址通常长这样：
- `192.168.x.x`
- `10.x.x.x`
- `172.16.x.x` 到 `172.31.x.x`

这些称为**私有 IP 范围**。它们保留给本地网络使用，永远不会在公网上出现。

### WAN——广域网

**WAN** = 你**家庭之外的**网络：就是互联网本身。

你的路由器有两面：
- **LAN 侧**面向家里的设备
- **WAN 侧**面向互联网服务提供商（ISP）

ISP 会给你的路由器分配一个公网 IP 地址用于 WAN 侧。家庭内部的所有设备都共享这一个公网 IP 来访问互联网。

```
[你的手机]--+
[你的笔记本]-+--[路由器]--[ISP]--[互联网]
[你的电视]---+
  （LAN 侧）      （WAN 侧）
```

### 网关

**网关**是将两个不同的网络连接在一起的设备。

在你的家里，路由器*就是*网关。你手机的 IP 是 `192.168.1.5`（LAN）。当你的手机想要访问 Google 的 `142.250.80.46`（WAN/互联网），它并不知道如何直接到达那里。于是它将数据发送给网关（路由器），由路由器想办法将其转发到互联网。

**规则：** 局域网上的每台设备都配置了一个"默认网关"——当设备不知道往哪发送数据包的时候，所有流量都会发送到的那个地址。

---

## 第二部分：数据究竟是如何送达的

### MAC 地址 vs IP 地址

实际上网络中有*两种*地址：

| 类型 | 外观 | 用途 |
|---|---|---|
| **IP 地址** | `192.168.1.5` | 逻辑地址——用于跨网络路由 |
| **MAC 地址** | `a4:c3:f0:12:34:56` | 物理地址——用于在**同一**网络上传递 |

可以这样理解：
- IP 地址是**城市和街道**——用于跨国家的导航
- MAC 地址是**门牌号**——到达大楼后使用的最终定位

当你的笔记本电脑向路由器发送数据包时，会使用路由器的 MAC 地址（因为它们在同一局域网上）。然后路由器使用 IP 地址来确定下一步发送到哪里。

### 什么是数据包？

在网络上传输的数据被分割成小块，称为**数据包**。每个数据包包含：
- 它从哪里来（源 IP）
- 它要去哪里（目的 IP）
- 实际数据的一小部分

网络在目的地将所有数据包重新组装起来。

---

## 第三部分：DHCP（你是如何获取 IP 地址的）

### 问题

每台设备都需要一个 IP 地址才能加入网络。但不能有两台设备使用相同的 IP——就像两个房子有同样的邮政地址一样，信件会丢失。

你*可以*手动为每台设备分配唯一的 IP，但那会很痛苦。如果有 50 台设备怎么办？

### 解决方案：DHCP

**DHCP** = 动态主机配置协议

它是一种由一个设备（**DHCP 服务器**）自动为每个加入网络的新设备分发 IP 地址的系统。

对话过程如下：

```
新设备：       "有人在吗？我刚加入这个网络，需要一个 IP 地址。"
DHCP 服务器：   "我听到了。来，192.168.1.42拿着。另外，你的网关是 192.168.1.1，
                DNS 使用 1.1.1.1。你的租约持续 24 小时。"
新设备：       "收到，谢谢！"
```

新设备现在拥有了在网络中工作所需的一切：
- 自己的 IP 地址
- 网关地址（知道将流量发往何处）
- DNS 地址（接下来会解释）

在你家里，**路由器运行着 DHCP 服务器**。它为每一台连接的设备分配 IP。

在 **Droidspaces NAT 模式**中，Droidspaces 自身会运行一个迷你 DHCP 服务器，为容器分配 IP（在 `172.28.x.x` 范围内）。IP 是确定性的：它由容器名称派生而来，持久化到配置文件中，并且在每次启动时重新提供——因此同一个容器在重启后始终拥有相同的地址。

---

## 第四部分：DNS（域名如何变成地址）

### 问题

IP 地址很难记住。没有人会输入 `142.250.80.46` 来访问 Google。人们都是输入 `google.com`。

但计算机只理解 IP 地址。所以需要一个系统将人类可读的名称转换成 IP 地址。

### 解决方案：DNS

**DNS** = 域名系统

它基本上是互联网的电话簿。你给它一个名称（`google.com`），它返回一个 IP 地址（`142.250.80.46`）。

对话过程：
```
你的浏览器：    "google.com 的 IP 地址是什么？"
DNS 服务器：    "是 142.250.80.46"
你的浏览器：    "谢了。" [现在连接到 142.250.80.46]
```

每台设备都配置了一个 DNS 服务器地址。在大多数家庭网络中，路由器*就*是 DNS 服务器（它将你的查询转发给 ISP 的 DNS 或公共 DNS，如 `1.1.1.1`）。

在 **Droidspaces NAT 模式**中，Droidspaces 会在容器内写入 `resolv.conf` 文件，指向 DNS 服务器（默认 `1.1.1.1` 和 `8.8.8.8`，或通过 `--dns` 指定的地址）。同样的 DNS 服务器也会在 DHCP 租约中通告。

---

## 第五部分：NAT（你的路由器施展的魔法）

### 问题

ISP 只给你**一个**公网 IP 地址。但你家有 10 台设备。这 10 台设备如何同时使用互联网？

### 解决方案：NAT

**NAT** = 网络地址转换

你的路由器维护着一张秘密表格。当局域网内的一台设备向互联网发送数据包时，路由器会：
1. 将源 IP 从设备的私有 IP（`192.168.1.5`）改写为路由器的公网 IP
2. 记住是哪台设备发送的
3. 当回复从互联网返回时，将目的地址改写回设备的私有 IP 并转发给它

从互联网的视角看，你所有的家庭设备看起来都是*一个设备*：就是路由器。

```
[笔记本: 192.168.1.5] --发送数据包--> [路由器]
                                         |
                                         | 将源 IP 改写为公网 IP
                                         v
                                    [互联网]
                                         |
                                         | 回复回来了
                                         v
                                    [路由器]
                                         |
                                         | 将目的 IP 改写回 192.168.1.5
                                         v
                           [笔记本: 192.168.1.5]
```

### NAT 在 Droidspaces 中的应用

在 Droidspaces NAT 模式中，Droidspaces 的行为*和你的家庭路由器完全一样*——不过是面向容器：

- 容器获得一个私有 IP（`172.28.x.x`）
- Droidspaces 安装 iptables `MASQUERADE` 规则（这是 Linux 中实现 NAT 目标的名称），外加 FORWARD 接受规则和 MSS 钳制规则，确保流量真正流通
- 容器可以访问互联网；互联网看到的是 Android 的 IP，而不是容器的 IP
- Droidspaces 还会为容器运行一个嵌入式 DHCP 服务器并配置其 DNS
- 在 Android 上，后台路由监视器会自动检测活跃的互联网上行链路（通过读取内核的路由规则），并在活跃网络切换时（例如 Wi-Fi 到移动数据切换）立即将容器流量重新指向正确的接口

---

## 第六部分：网桥与虚拟线缆（Linux 管道）

现在我们深入一层，看看 Linux 究竟是如何将容器彼此连接起来的。

### 什么是网桥？

**网桥**的工作原理类似于网络交换机。物理网络交换机是一个盒子，可以插入多根以太网线缆；所有连接到它的设备都可以相互通信。

Linux 的**网桥**就是一个虚拟交换机，完全由软件实现。你可以用一条命令创建它，然后将虚拟网络接口"插入"其中。

```
物理世界：                  Linux 世界：
+--------------+          +--------------+
|   交换机     |          |    网桥     |  （纯软件，没有物理盒子）
| 端口1  端口2 |          | 端口1  端口2 |
+--+------+---+          +--+------+---+
   |      |                  |      |
[PC1]  [PC2]           [veth1]  [veth2]   （虚拟线缆）
```

### 什么是 veth 对？

**veth** = 虚拟以太网

veth 对是一对互相连接的虚拟网络接口，就像一根管道。你从一端发送的任何东西都会从另一端出来。

可以把它想象成一根有两端插头的虚拟以太网线缆。你把一端插在容器内部，另一端留在宿主机上（或插入一个网桥）。

```
[容器 netns]          [宿主机 netns]
     eth0 --------------------- ds-veth0
  （容器内的插头）   （宿主机侧的插头）
```

### NAT 模式如何使用网桥和 veth

在 Droidspaces NAT 模式中：

```
[容器 netns]
     eth0（例如 172.28.137.42）
      |
      | veth 对（虚拟线缆）
      |
[宿主机侧]
     ds-v<PID> ---- ds-br0（网桥，IP 为 172.28.0.1）
                      |
                  iptables MASQUERADE
                      |
                  wlan0 / rmnet0
                 （Android 的真实网络）
```

网桥 `ds-br0` 拥有网关 IP `172.28.0.1`，每个 NAT 容器都将其作为默认网关使用。veth 对以容器的 init 进程 ID 命名：宿主机侧为 `ds-v<PID>`，容器侧初始为 `ds-p<PID>`，然后在容器内重命名为 `eth0`。

Droidspaces 运行一个小型的给每个容器开放的 DHCP 服务器，监听在容器的宿主机侧 veth 上。整个 `172.28.0.0/16` 子网属于 Droidspaces（`172.28.0.x` 段保留给网关自身，所以容器始终落在 `172.28.1.x` 到 `172.28.254.x` 之间），其中所有内容通过 Android 的真实接口进行 NAT 出站。

---

## 第七部分：网络命名空间（容器是如何隔离的）

### 什么是命名空间？

Linux 有一个名为**命名空间**的功能，可以让你创建系统资源的隔离视图。

**网络命名空间**是整个网络栈的一份隔离副本。它拥有自己的：
- 网络接口
- 路由表
- iptables 规则
- 与网络相关的一切

当 Droidspaces 启动一个容器时，会为它创建一个新的网络命名空间。容器住在该命名空间中。它完全看不到宿主机的网络接口——只能看到 Droidspaces 显式放入其命名空间内的东西。

veth 对是宿主机命名空间和容器命名空间之间的隧道：
- veth 的一端进入容器的网络命名空间（显示为 `eth0`）
- 另一端留在宿主机的网络命名空间中（Droidspaces 将其连接到网桥）

---

## 第八部分：OpenWRT 及其概念

### 什么是 OpenWRT？

**OpenWRT** 是专为路由器设计的 Linux 发行版。通常情况下它运行在物理路由器硬件上，但也可以在常规 Linux 系统或容器内运行。

当 OpenWRT 运行时，它提供：
- **netifd**——网络接口守护进程（管理网络接口、DHCP 客户端/服务器等）
- **dnsmasq**——DNS 和 DHCP 服务器
- **firewall3** 或 **nftables**——防火墙
- **LuCI**——配置用的 Web 界面
- 真正路由器所做的一切，以软件形式实现

这意味着你可以在 Droidspaces 容器内运行 OpenWRT，它会表现得和真正的路由器一模一样：管理网络、分发 DHCP 租约、处理 DNS、应用防火墙规则、路由 VPN 流量等。

---

## 第九部分：全新的网关模式——融会贯通

### 为什么要有网关模式？

在 NAT 模式中，Droidspaces 就是路由器。它包办一切。这在大多数情况下都很好。

但如果你想让 **OpenWRT 成为其他容器的路由器**呢？你想要 OpenWRT 的防火墙规则、OpenWRT 的 DHCP、OpenWRT 的 VPN 路由，并让其他容器（比如 Kali Linux 容器）处在 OpenWRT 的局域网内，一切从 OpenWRT 获取。

问题在于：如果 Droidspaces 也试图为这些容器安装 NAT、DHCP 和 DNS，就会与 OpenWRT 正在做的事情*冲突*。两个 DHCP 服务器争抢由谁分配 IP 地址。两个防火墙应用互相矛盾的规则。

**网关模式解决了这个问题。** Droidspaces 退后一步。它只做 L2 的管道（虚拟线缆和交换机），让 OpenWRT 掌管所有策略：DHCP、DNS、防火墙、路由。

### 架构总览

```
Android 宿主机内核
|
+-- wlan0（Android 的真实 Wi-Fi——WAN）
|
+-- [OpenWRT 容器——net=nat 模式]
|    netns：拥有 eth0（WAN 侧，由 Droidspaces 提供 NAT）
|           eth1（LAN 侧——由网关模式插入 ds-lan 网桥）
|    运行：dnsmasq、netifd、防火墙、VPN
|
+-- ds-lan（宿主机网桥——无 IP 地址，仅作为交换机）
|    |
|    +-- ds-g[hash]（veth 宿主机侧，连接到 OpenWRT 的 netns 作为 eth1）
|    +-- ds-v[pid]  （veth 宿主机侧，连接到 Kali 的 netns 作为 eth0）
|
+-- [Kali 容器——net=gateway 模式]
     netns：拥有 eth0（LAN 侧——插入 ds-lan 网桥）
     从 OpenWRT 的 dnsmasq 获取 DHCP
     路由决策由 OpenWRT 做出
     防火墙规则由 OpenWRT 应用
```

### 逐步说明——启动网关模式容器时发生了什么

**第 1 步——先启动 OpenWRT（NAT 模式）**

```bash
droidspaces --name=openwrt --rootfs=/data/openwrt --net=nat start
```

OpenWRT 启动。它拥有：
- WAN 侧的 `eth0`（Droidspaces 为其管理 NAT）
- 还没有 LAN 侧——OpenWRT 正在等待

**第 2 步——启动 Kali（网关模式）**

```bash
droidspaces --name=kali --rootfs=/data/kali --net=gateway --gateway=openwrt start
```

Droidspaces 执行以下操作（仅管道，无策略）：

1. 找到 OpenWRT 正在运行的进程 ID，以便访问其网络命名空间
2. 在宿主机上创建一个名为 `ds-lan` 的网桥，该网桥上不设置 IP 地址
3. 禁用 `bridge-nf-call-iptables`，使 Android 宿主机防火墙**不**拦截该网桥上的流量，让 OpenWRT 的防火墙成为唯一的权威
4. 为 OpenWRT 的 LAN 侧创建一个 veth 对——一端进入 OpenWRT 的 netns（显示为 `eth1`），另一端插入 `ds-lan` 网桥
5. 为 Kali 创建一个 veth 对——一端进入 Kali 的 netns（显示为 `eth0`），另一端插入 `ds-lan` 网桥
6. **不**安装 NAT、DHCP、DNS 或任何防火墙规则

**第 3 步——OpenWRT 接管**

OpenWRT 的 `netifd` 检测到 `eth1` 出现。它将其配置为 LAN 接口。
OpenWRT 的 `dnsmasq` 开始在 `eth1` 上响应 DHCP 请求。

Kali 的 `eth0` 发送 DHCP 请求，OpenWRT 的 `dnsmasq` 回复：
- IP 地址：`192.168.1.100`（或 OpenWRT DHCP 范围内的任意 IP）
- 网关：`192.168.1.1`（即 OpenWRT 本身）
- DNS：`192.168.1.1`（OpenWRT 的 dnsmasq）

Kali 现在已完全配置好，以 OpenWRT 作为其路由器。

**第 4 步——流量经过 OpenWRT**

当 Kali 尝试访问互联网时：

```
Kali eth0 --> ds-lan 网桥 --> OpenWRT eth1
                                   |
                          OpenWRT 防火墙规则在此应用
                                   |
                          OpenWRT 路由到 eth0（WAN）
                                   |
                          Droidspaces NAT（eth0 -> wlan0）
                                   |
                              Android wlan0 --> 互联网
```

OpenWRT 的防火墙看到 Kali 的全部流量，并可以应用任何规则：阻止特定网站、通过 VPN 重定向、带宽整形、连接日志——完全和真正的路由器一样。

### "延迟挂接"的含义

网关 veth 是"延迟挂接"的。这意味着：

- 当你启动 OpenWRT 时，它**不会**立即获得 `eth1`
- `eth1` 仅在**第一个网关模式容器启动时**才会出现在 OpenWRT 内部
- 这是有意为之——OpenWRT 只带着它的 WAN 侧启动（`eth0`），其 LAN 线缆（`eth1`）稍后按需插入

这模拟了你在路由器运行后，再将物理线缆插入路由器 LAN 端口的场景。

### 为什么网关模式下不修改 resolv.conf

在 NAT 模式中，Droidspaces 会在容器内写入 `/etc/resolv.conf`，指向 `1.1.1.1` 或 `8.8.8.8`。

在网关模式中，Droidspaces **不**写入 `resolv.conf`（除非你显式传入 `--dns`）。这是因为 OpenWRT 的 `dnsmasq` 会通过 DHCP 租约将 DNS 服务器地址分发给容器。如果 Droidspaces 也写入了 `resolv.conf`，就会与 dnsmasq 提供的内容冲突——容器将使用错误的 DNS，完全绕过 OpenWRT 的 DNS 过滤/缓存。

### 为什么 bridge-nf-call-iptables 要设为 0

网桥 `ds-lan` 承载着 OpenWRT 和 Kali 之间的流量。默认情况下，Linux 可以将桥接流量通过宿主机的 iptables 处理。这意味着 Android 的 iptables 规则（可能会意外丢弃或 NAT 某些流量）会干扰本该由 OpenWRT 管理的流量。

将其设为 `0` 告诉 Linux："不对桥接流量运行 iptables。"这让 OpenWRT 的防火墙成为查看此流量的*唯一*防火墙——这正是我们想要的效果。

### 网关容器必须先运行

启动顺序很重要。当一个网关模式容器启动时，Droidspaces 会查找网关容器的实时进程 ID 以访问其网络命名空间。如果此时网关容器没有运行，网络设置会失败并发出警告，客户端容器依然会启动——但完全没有网络（只有 loopback）。它不会自行重试。

同样的逻辑也适用于网关重启后。veth 对是一起死亡的：当网关容器停止时，它内部的 `eth1` 端被销毁，这也会销毁宿主机侧的那一端。现有的客户端仍然插在网桥上，但已经没有了路由器。LAN 线缆会在该网段上**任何**网关模式容器的下一次启动时重新插入——所以在重启网关后，重启一个客户端（或启动一个新客户端）即可让网段恢复。

### 容器停止时会发生什么

网关模式中的清理被刻意最小化，遵循"仅管道"的理念：

- **客户端停止：** 仅移除该客户端自己的 veth（`ds-v<PID>`）。网桥和网关的 `eth1` 保持运行，因此同一网段上的其他客户端不受影响。
- **网关停止：** 网关侧的 veth 随其命名空间一起消失（见上文），但网桥本身保留。
- 委托网桥（`ds-lan` 等）永远不会被 Droidspaces 拆除。它在闲置时无害——没有 IP 也不承载任何策略——并持续存在直到你手动删除它或重启设备。

---

## 第十部分：网关模式的标志与配置

### 必选标志 vs 可选标志

使用 `--net=gateway` 时**只有一个标志是必选的**：

```bash
--gateway=<container_name>
```

如果省略它，Droidspaces 会打印错误并拒绝启动。其他所有标志都有可用的默认值：

| 标志 | 默认值 | 它控制什么 |
|---|---|---|
| `--gateway=NAME` | *（无——必选）* | 哪个运行中的容器是路由器 |
| `--gateway-net=NAME` | `lan` | LAN 网段名称——见下文 |
| `--gateway-iface=IFACE` | `eth1` | 网关容器内的接口名称 |
| `--gateway-bridge=BR` | `ds-{gateway-net}` | 完全覆盖宿主机网桥名称 |

因此最小有效命令是：

```bash
droidspaces --name=client --net=gateway --gateway=openwrt start
```

这与显式拼出所有默认值完全相同：

```bash
droidspaces --name=client --net=gateway --gateway=openwrt \
  --gateway-net=lan \
  --gateway-iface=eth1 \
  start
```

### --gateway-net 的作用

该标志同时控制两件事，均从同一个名称派生。

**1. 它命名宿主机网桥。**

Droidspaces 在宿主机上创建的网桥命名为 `ds-{NAME}`：

```
--gateway-net=lan   ->  宿主机网桥：ds-lan
--gateway-net=vpn   ->  宿主机网桥：ds-vpn
--gateway-net=iot   ->  宿主机网桥：ds-iot
```

**2. 它是网段标识符——客户端落在哪个网桥上。**

网关 LAN 侧的 veth 名称是通过对字符串 `{gateway_container}:{gateway_net}` 进行哈希来生成的。相同的哈希 = 相同的 veth = 相同的网桥网段。这意味着共享相同 `--gateway` 和 `--gateway-net` 的多个客户端容器最终都在同一个网桥上，并且都从同一个 OpenWRT 接口获取 DHCP。

这就是 `--gateway-net` 真正的威力所在：通过同一个网关容器运行多个隔离的 LAN 网段。

```bash
# 这两个落在 ds-lan 上——它们彼此可见，OpenWRT 将它们作为一个局域网路由
droidspaces --name=kali   --net=gateway --gateway=openwrt --gateway-net=lan start
droidspaces --name=ubuntu --net=gateway --gateway=openwrt --gateway-net=lan start

# 这一个落在 ds-vpn 上——一个完全独立的网桥
# OpenWRT 可以对这个网段应用不同的防火墙/VPN 规则
droidspaces --name=torbox --net=gateway --gateway=openwrt --gateway-net=vpn start
```

在 OpenWRT 内部，`lan` 客户端通过 `eth1` 传入，`vpn` 客户端通过 `eth2` 传入（每个网段获得自己的 veth，因为 `openwrt:lan` 和 `openwrt:vpn` 的哈希不同）。

### --gateway-iface 的作用

这控制了**LAN 接口在网关容器的网络命名空间内部使用的名称**。

当 Droidspaces 为一个网段创建网关 veth 时，它会将一端移入 OpenWRT 的 netns，并将其从原始的哈希名称（`ds-hXXXXXXXX`）重命名为你在此传入的名称（默认 `eth1`）。

**这为什么重要？** OpenWRT 的配置是围绕接口名称构建的。如果你的 OpenWRT `/etc/config/network` 写着：

```
config interface 'lan'
    option device 'eth1'
```

...那么出现在 OpenWRT 内部的接口**必须**命名为 `eth1`，否则 OpenWRT 不会将其识别为 LAN，也不会在上面提供 DHCP。`--gateway-iface=eth1` 确保了这一点。

对于第二个网段，你需要传入 `--gateway-iface=eth2`，这样 OpenWRT 会将其视为一个独立的接口，你可以为其添加第二个 UCI network 配置块。

**重要细节：** `--gateway-iface` 仅在该网段的网关 veth 首次创建时生效——也就是该网段上第一个客户端容器启动时。网关 veth 由同一个 `--gateway-net` 上的所有客户端共享；它只创建一次并重复使用。第一个之后的每个客户端完全跳过网关 veth 的创建，仅仅将自己的应用 veth 接入现有的网桥。

这意味着如果你在 `--gateway-net=lan` 上启动两个容器，且两个都传入 `--gateway-iface=eth1`，完全没有问题：第一个容器创建 veth 并重命名为 `eth1`，第二个容器发现 veth 已经存在，根本不处理 `--gateway-iface`。

### 必须避免的标志冲突

问题只会在你使用**两个不同的 `--gateway-net` 网段但相同的 `--gateway-iface`** 时出现：

```bash
# 网段 1——在 OpenWRT 内部创建 eth1
droidspaces --name=kali   --net=gateway --gateway=openwrt --gateway-net=lan --gateway-iface=eth1 start

# 网段 2——错误：也尝试在 OpenWRT 内部创建 eth1
droidspaces --name=torbox --net=gateway --gateway=openwrt --gateway-net=vpn --gateway-iface=eth1 start
```

当第二条命令运行时，Droidspaces 尝试将一个新的 veth 对端移入 OpenWRT 并重命名为 `eth1`。但 `eth1` 已经存在于 OpenWRT 内部（来自第一个网段）。代码不会直接报错，而是检测到冲突后仅将现有的 `eth1` 重新启用，让新的 veth 对端以其原始哈希名称（`ds-hYYYYYYYY`）留在 OpenWRT 中。OpenWRT 没有 `ds-hYYYYYYYY` 的配置，会静默忽略它。`vpn` 网段得不到网关侧接口：没有 DHCP、没有路由，其上的容器实际上处于隔离状态。

**规则：** 每个 `--gateway-net` 网段必须有唯一的 `--gateway-iface` 名称。

```bash
# 正确：两个网段，两个接口名称
--gateway-net=lan  --gateway-iface=eth1   ->  OpenWRT 内的 eth1（LAN 网段）
--gateway-net=vpn  --gateway-iface=eth2   ->  OpenWRT 内的 eth2（VPN 网段）
```

### 验证规则与内核要求

Droidspaces 在启动时强制执行几条规则，违规则拒绝启动：

- 容器不能将自己作为自己的网关（`--gateway` 必须命名一个不同的容器）
- 接口和网桥名称必须短于 16 个字符（Linux `IFNAMSIZ` 限制），且只能包含字母、数字、`_` 和 `-`
- 内核必须支持网络命名空间（`CONFIG_NET_NS`）、veth 对（`CONFIG_VETH`）和网桥（`CONFIG_BRIDGE`）。Droidspaces 在启动前检查这三项，缺少任一则致命错误退出

另外还有两点需要了解：

- `--port` 仅在 NAT 模式中有意义。在网关模式中，它会被忽略并发出警告——端口转发和上行链路选择现在是网关容器的工作。
- 当宿主机网桥名称从 `--gateway-net` 自动派生时，名称会被清理（仅保留字母、数字、`_`、`-`）并截断为 9 个字符，生成 `ds-` 加上最多 9 个字符。如果需要精确的网桥名称，请使用 `--gateway-bridge` 显式设定。

---

## 第十一部分：所有网络模式对比

| 特性 | NAT 模式 | Host 模式 | None 模式 | 网关模式 |
|---|---|---|---|---|
| 谁分配 IP？ | Droidspaces DHCP | Android（共享） | 无（仅 loopback） | OpenWRT dnsmasq |
| 谁做 NAT？ | Droidspaces iptables | Android | 不适用 | OpenWRT（通过 Droidspaces 为 OpenWRT 的 WAN 做 NAT） |
| 谁管理防火墙？ | Droidspaces | Android | 不适用 | OpenWRT |
| 谁管理 DNS？ | Droidspaces | Android | 无 | OpenWRT dnsmasq |
| 容器与宿主机网络隔离？ | 是 | 否 | 是 | 是 |
| 互联网访问？ | 是 | 是 | 否 | 是（通过网关容器） |
| 需要第二个容器才能工作？ | 否 | 否 | 否 | 是（网关容器） |
| 适用于 | 简单的互联网访问 | 最大性能，零开销 | 离线 / 沙盒环境 | 路由器设备、VPN 网关、分段局域网 |

---

## 第十二部分：网关模式的真实使用场景

### 1. 为特定容器设置 VPN 终止开关

运行带有 WireGuard 或 OpenVPN 客户端的 OpenWRT。配置 OpenWRT 的防火墙丢弃所有不经过 VPN 隧道的流量。使用网关模式的任何容器都无法将流量泄漏到 VPN 之外——OpenWRT 在网桥层面强制实施这一点，而不是在各个容器内部。

### 2. 多个隔离的 LAN 网段

使用 `--gateway-net` 在同一个 OpenWRT 上创建独立的网段。`--gateway-net=lan` 上的容器无法访问 `--gateway-net=vpn` 上的容器，除非 OpenWRT 显式地在它们之间进行路由。你可以通过一个网关容器实现类 VLAN（虚拟局域网）风格的隔离。

### 3. 流量分析

运行带有 `tcpdump` 或 `nftables` 日志功能的 OpenWRT。来自每个网关模式容器的每个数据包都流经 OpenWRT，因此你获得了一个单一的咽喉点，可以同时观察所有容器的全部网络活动。

### 4. 自定义 DNS 过滤

运行带有 `dnsmasq` 黑名单（或通过 opkg 安装 Adblock）的 OpenWRT。网关局域网上的每个容器都会获得经过过滤的 DNS，而无需单独触碰每个容器。

### 5. 带宽整形

OpenWRT 的 `tc`（流量控制）和 `sqm-scripts` 可以按容器进行带宽整形，因为 OpenWRT 将每个容器视为到达其 LAN 接口的一个独立 MAC 地址。

---

## 快速参考——术语表

| 术语 | 一句话定义 |
|---|---|
| **IP 地址** | 网络上设备的数字地址（例如 `192.168.1.5`） |
| **MAC 地址** | 网络接口的硬件地址，用于同一网络内的传递 |
| **LAN** | 局域网——彼此靠近、可以直接通信的设备 |
| **WAN** | 广域网——互联网，本地网络之外的部分 |
| **网关** | 连接两个网络并在它们之间路由流量的设备 |
| **DHCP** | 自动为设备分配 IP 地址的协议 |
| **DNS** | 将人类可读名称（`google.com`）转换为 IP 地址的系统 |
| **NAT** | 将一个公网 IP 在多个私有 IP 设备之间共享的技术 |
| **网桥** | 连接多个网络接口的虚拟（或物理）交换机 |
| **veth 对** | 一对像管道一样连接的虚拟网络接口——从一端进，从另一端出 |
| **网络命名空间** | Linux 网络栈的隔离副本——容器生活在自己的命名空间中 |
| **OpenWRT** | 一个为作为路由器/网关运行而设计的 Linux 发行版——运行 dnsmasq、netifd、防火墙 |
| **netifd** | OpenWRT 的网络接口守护进程——管理接口和 DHCP |
| **dnsmasq** | OpenWRT 使用的轻量级 DHCP 和 DNS 服务器 |
| **MASQUERADE** | 实现 NAT 的 Linux iptables 规则（改写源 IP） |
| **委托 LAN** | Droidspaces 在网关模式中创建的网桥网络——策略由网关容器而非 Droidspaces 掌管 |
| **网段** | 由 `--gateway-net` 标识的一个隔离 LAN——每个网段获得自己的网桥和在网关容器内的独立接口 |
| **延迟挂接** | 网关的 LAN 侧 veth 仅在第一个客户端容器启动时创建，而非网关容器启动时创建 |
