<!--
title: 从零开始的网络基础
section: 指南
order: 5
desc: 面向初学者的 Droidspaces 网关模式背后每个网络概念指南——IP 地址、LAN、WAN、DHCP、DNS、NAT、网桥、veth 对、网络命名空间与 OpenWRT。
keywords: droidspaces, networking, gateway, openwrt, nat, dhcp, dns, lan, wan, veth, bridge, namespace, linux, android
-->

# 从零开始的网络基础：理解 Droidspaces 网关模式

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
- 容器同时获得 IPv6。Droidspaces 通过路由通告 (RA) 发布一个私有前缀（`fd64:7370::/64`），容器据此自行配置地址，出站时由 IPv6 版本的 `MASQUERADE` 进行转换。这需要内核提供 IPv6 NAT 表，否则容器仅有 IPv4
- 在 Android 上，后台路由监视器会自动检测活跃的互联网上行链路（通过读取内核的路由规则），并在活跃网络切换时（例如 Wi-Fi 到移动数据切换）立即将容器流量重新指向正确的接口

### NAT 模式如何选择 WAN 上行链路（自动）

NAT 容器必须知道 Android 哪个真实接口当前可以访问互联网，这样才能通过它做 MASQUERADE。手机让这件事变得棘手：活跃网络会在 Wi-Fi、移动数据、USB 以太网和 VPN 隧道之间跳转，接口名称本身也不稳定（移动数据接口这一分钟可能是 `rmnet0`，重连后可能变成 `rmnet8`）。

默认情况下 Droidspaces 会为你处理这些事情——**无需任何配置**：

- 它读取内核自己关于“当前互联网接口是什么”的事实来源。在 Android 上，这是 `netd` 为活跃默认网络安装的策略路由规则；在桌面 Linux 上则是主路由表的默认路由。
- 后台**路由监控器**订阅内核路由事件（规则、路由、链路和地址变化）。宿主一切换网络——例如你走出 Wi-Fi 范围转到移动数据，或者在笔记本上插入 USB 以太网适配器——监控器会立即重定向容器流量。无需重启，也无需配置。
- CLAT/464xlat 接口（手机在纯 IPv6 移动网络上合成的 `v4-rmnet...` 接口）会被自动识别。

这种自动模式适合绝大多数用户。本节剩余内容仅在你想要*覆盖*默认行为时才相关。

### 使用 `--upstream` 手动固定上行链路

有时候你**不**希望容器跟随宿主正在使用的网络。你想强制它只通过某个特定接口访问互联网，并保持在那里。这就是 `--upstream` 的作用。

传入 `--upstream` 后，自动检测会被完全关闭。你列出的接口会成为容器唯一可用的 WAN 候选；它永远不会跳到宿主标记为活跃默认网络的其他接口。

```bash
# 始终强制容器通过 Wi-Fi 出网
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=wlan0 start
```

你可以列出**多个接口，用逗号分隔**，并使用**通配符**（`*`、`?`）：

```bash
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=wlan0,rmnet* start
```

列表是**按优先级排序**的。路由监控器会从上到下检查，并使用第一个当前已启用且确实有互联网的接口。因此 `wlan0,rmnet*` 表示“优先 Wi-Fi；Wi-Fi 断开时回退到移动数据”——Wi-Fi 恢复后再切回。故障转移严格限定在*你的列表内部*；它不会回退到你没有列出的接口。这是与自动模式的关键区别：WAN 由你决定，而不是由宿主决定。

如果固定接口在容器启动时不存在，或会话中途消失后又重新出现，也能处理：在某个固定接口可用之前，容器只是没有 WAN；接口可用后会自动接线。

> **为什么移动数据需要通配符：** Android 不会给移动数据接口稳定编号——它可能是 `rmnet0`、`rmnet8`、`rmnet_data2`，并且重连后编号会变化。固定字面量 `rmnet0` 下次就可能失效。固定 `rmnet*` 才能持续工作。

### `--upstream` 的使用场景

**1. 让容器 WAN 走 Android VPN（`tun0`）**

先在手机本身连接 VPN——ProtonVPN、WireGuard、OpenVPN，任何会创建 `tun0` 接口的应用都可以。然后把容器固定到它：

```bash
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=tun0 start
```

现在 VPN 断开时，`tun0` 消失，容器不再有 WAN。它不会静默回退到移动数据或 Wi-Fi。VPN 恢复时，路由监控器会重新接线。

**2. 手机在 Wi-Fi 上，但容器走移动数据**

在开发者选项中启用“始终开启移动数据”，保持 Wi-Fi 连接，同时打开移动数据，然后运行：

```bash
droidspaces --name=box --rootfs=/data/box --net=nat --upstream=rmnet* start
```

宿主手机可以继续通过 Wi-Fi 使用互联网，而容器会被固定到蜂窝接口。

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
+-- [Kali 容器——网关模式（net=gateway）]
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

如何接线取决于客户端使用的 init 系统：

- **systemd 容器：** `/etc/resolv.conf` 是指向 `/run/systemd/resolve/resolv.conf` 的符号链接，由 systemd-resolved 根据 DHCP 租约填充。
- **非 systemd 容器：** Droidspaces 完全不碰 `/etc/resolv.conf`，由容器自己的 DHCP 客户端（udhcpc/dhclient）根据网关的租约写入 nameserver。早期版本会在这里硬编码写入 `1.1.1.1`/`8.8.8.8`，静默绕过了网关的 DNS；该问题已修复。如果精简 rootfs 没有自带 DHCP 的 resolv.conf 钩子，可传入 `--dns` 显式设置。

### 为什么 bridge-nf-call-iptables 要设为 0

网桥 `ds-lan` 承载着 OpenWRT 和 Kali 之间的流量。默认情况下，Linux 可以将桥接流量通过宿主机的 iptables 处理。这意味着 Android 的 iptables 规则（可能会意外丢弃或 NAT 某些流量）会干扰本该由 OpenWRT 管理的流量。

将其设为 `0` 告诉 Linux："不对桥接流量运行 iptables。"这让 OpenWRT 的防火墙成为查看此流量的*唯一*防火墙——这正是我们想要的效果。

### 启动顺序与自动自愈

网关模式客户端的所有接线都由宿主侧的单个函数 `gateway_wire_client()` 完成：它确保网桥和网关侧线缆存在，创建客户端的应用 veth，其对端直接在客户端命名空间中以 `eth0` 的名称创建（固定 MAC，置为 up）；客户端自己的启动代码只启用 `lo`。因为宿主拥有每一步，同一个函数既可以为刚启动的客户端接线，也可以为已经运行的客户端接线。

这给出了一个围绕**网关存活状态**的简单规则：

- **客户端启动时网关已经运行** → 客户端立即接线（它自己的监控器调用 `gateway_wire_client`）。
- **客户端启动时网关尚未运行** → 客户端**完全不接线**（没有网桥、没有 veth、没有 `eth0`），只正常启动。工作会完全推迟给网关。

因此，自愈由**网关自身**驱动，而不是由客户端驱动。网关容器每次启动周期中，其监控器都会调用 `ds_net_rewire_gateway_clients()`：扫描正在运行的容器，找到委托给这个网关的客户端，并为每个客户端运行 `gateway_wire_client`——在网关*当前*命名空间中建立网关侧 `eth1` 线缆，并把每个客户端的 `eth0` 接入。所以当网关**启动或重启**时，所有运行中的客户端都会被重新接线，**无需重启客户端**。

网关关闭时跳过所有客户端接线（而不是半接一个网桥和悬空 veth）也避免了一类竞态：先于网关启动的客户端，可以在网关起来之前编辑自己的网关/LAN 设置（`--gateway-net`、`--gateway-bridge` 等）；网关随后会从每个客户端的*当前*配置为所有客户端接线，而不是使用过期配置。

网关是主要的执行者，但有一个例外：双方都通过对方的 pidfile 判断对方是否在运行，而 pidfile 要等该容器的 init 启动后才出现。因此客户端与网关同时启动时，双方可能都认为对方未运行，结果谁都不给客户端接线。为此，尚未接线的客户端大约每两秒再检查一次，一旦网关运行就自行接线，随后停止检查。每个网段的接线都会串行化（咨询式文件锁），以避免并发客户端启动与网关重连互相竞争。`eth1`（网关侧）以及每个 `eth0`（客户端侧）都保持**稳定 MAC**，并在创建 veth 对的同一个请求中直接以最终名称在各自命名空间内创建，之后不再移动或重命名：内核会在设备能按新名称查到之前就广播重命名事件，网关内先听到该事件的 `netifd` 会永久无法接管该设备（在 4.14 内核上观察到，间隔达数十毫秒）。

### 容器停止时会发生什么

网关模式中的清理被刻意最小化，遵循"仅管道"的理念：

- **客户端停止：** 仅移除该客户端自己的 veth（网关客户端的宿主侧 veth 名为 `ds-c` 加容器名称的哈希，与 NAT 的 `ds-v<PID>` 区分，这样崩溃实例遗留的 veth 能被下一次启动找到并替换）。网桥和网关的 `eth1` 保持运行，因此同一网段上的其他客户端不受影响。
- **最后一个客户端停止但网关仍在运行：** 网桥会被**保留**（不会回收）。拆掉它会让网关中正在运行的 `eth1` 载波抖动，并偶尔让 netifd 报告“device initialization failed”；一个闲置、无 IP 的网桥是无害的，下一个客户端会复用它。
- **网关停止：** Droidspaces 会主动删除网关侧 veth。这不能交给内核：veth 的宿主端持有其对端命名空间的引用，已停止网关的命名空间将永远无法释放。一旦没有客户端且网关也已停止，现在闲置的网桥会被回收。

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

当 Droidspaces 为一个网段创建网关 veth 时，网关这一端直接在 OpenWRT 的 netns 中以你在此传入的名称（默认 `eth1`）创建；只有宿主端带有哈希名称（`ds-gXXXXXXXX`）。

**为什么要这么做？** OpenWRT 的配置是围绕接口名称构建的。如果你的 OpenWRT `/etc/config/network` 写着：

```
config interface 'lan'
    option device 'eth1'
```

...那么出现在 OpenWRT 内部的接口**必须**命名为 `eth1`，否则 OpenWRT 不会将其识别为 LAN，也不会在上面提供 DHCP。`--gateway-iface=eth1` 确保了这一点。

对于第二个网段，你需要传入 `--gateway-iface=eth2`，这样 OpenWRT 会将其视为一个独立的接口，你可以为其添加第二个 UCI network 配置块。

**重要细节：** `--gateway-iface` 仅在该网段的网关 veth 首次创建时生效——也就是该网段上第一个客户端容器启动时。网关 veth 由同一个 `--gateway-net` 上的所有客户端共享；它只创建一次并重复使用。第一个之后的每个客户端完全跳过网关 veth 的创建，仅仅将自己的应用 veth 接入现有的网桥。

这意味着如果你在 `--gateway-net=lan` 上启动两个容器，且两个都传入 `--gateway-iface=eth1`，完全没有问题：第一个容器创建 veth，其对端在 OpenWRT 内名为 `eth1`，第二个容器发现 veth 已经存在，根本不处理 `--gateway-iface`。

### 必须避免的标志冲突

问题只会在你使用**两个不同的 `--gateway-net` 网段但相同的 `--gateway-iface`** 时出现：

```bash
# 网段 1——在 OpenWRT 内部创建 eth1
droidspaces --name=kali   --net=gateway --gateway=openwrt --gateway-net=lan --gateway-iface=eth1 start

# 网段 2——错误：也尝试在 OpenWRT 内部创建 eth1
droidspaces --name=torbox --net=gateway --gateway=openwrt --gateway-net=vpn --gateway-iface=eth1 start
```

当第二条命令运行时，Droidspaces 发现 OpenWRT 内已经有属于第一个网段的 `eth1`，于是拒绝为 `vpn` 网段接线，日志中会出现 `'openwrt' already has an interface named eth1 that is not this segment's cable`。`vpn` 网段得不到网关侧接口：没有 DHCP、没有路由，其上的容器实际上处于隔离状态。

**规则：** 每个 `--gateway-net` 网段必须有唯一的 `--gateway-iface` 名称。

```bash
# 正确：两个网段，两个接口名称
--gateway-net=lan  --gateway-iface=eth1   ->  OpenWRT 内的 eth1（LAN 网段）
--gateway-net=vpn  --gateway-iface=eth2   ->  OpenWRT 内的 eth2（VPN 网段）
```

### 通过网关使用 IPv6

NAT 模式除了 IPv4 之外也会给容器提供 IPv6：Droidspaces 通过路由通告 (RA) 发布一个私有 IPv6 前缀（`fd64:7370::/64`），并在出站时进行地址转换，原理与 IPv4 NAT 相同。网关容器的 WAN 口就是一个 NAT 接口，所以 OpenWRT 也可以获得 IPv6。但它不会自动配置好，开始之前需要先了解两点：

- **Droidspaces 只给 OpenWRT 一个地址，而不是一段可以继续分发的地址。** 因此 OpenWRT 用自己的私有前缀 (ULA) 为 LAN 编址，并在 WAN 口再做一次 IPv6 NAT。客户端流量会被转换两次，一次由 OpenWRT，一次由 Droidspaces。这样可以正常工作，只是与真实运营商线路上使用 IPv6 的方式不同。
- **两者都依赖宿主内核。** 容器共享宿主的内核，所以 Droidspaces 和 OpenWRT 都需要内核启用 `CONFIG_IP6_NF_NAT` 和 `CONFIG_IP6_NF_TARGET_MASQUERADE`。`droidspaces check` 会以 "IPv6 NAT support" 显示这一项。

Droidspaces rootfs 仓库提供的 OpenWRT 镜像已经包含第 1、2、4 步，使用它时只需为每个 LAN 执行第 3 步。其他 OpenWRT 镜像需要在网关容器内执行全部步骤。

**1. 在 `eth0` 上添加 IPv6 WAN**

```bash
uci set network.wan6=interface
uci set network.wan6.device='eth0'
uci set network.wan6.proto='dhcpv6'
uci set network.wan6.sourcefilter='0'
```

`sourcefilter '0'` 很重要。OpenWRT 默认把 IPv6 默认路由安装成"仅适用于源地址属于 WAN 前缀的数据包"。这会带来两个问题：Android 内核没有启用 `CONFIG_IPV6_SUBTREES`，这条路由会静默安装失败；而且 LAN 客户端的源地址本来就不同，这条路由对它们也不生效。

**2. 确认 OpenWRT 有自己的私有前缀**

```bash
uci get network.globals.ula_prefix
```

如果输出一个以 `fd` 开头的前缀，就不用再做什么。如果提示 "Entry not found"，就设置一个。任何带有 10 位随机十六进制数的 `fd` 前缀都可以：

```bash
uci set network.globals=globals
uci set network.globals.ula_prefix='fd3a:91c2:7b4e::/48'
```

**3. 为每个 LAN 启用 IPv6**

把 `lan` 换成你的接口名称。每个需要 IPv6 的 LAN 网段都要执行一次。

```bash
uci set network.lan.ip6assign='64'
uci set dhcp.lan.ra='server'
uci set dhcp.lan.dhcpv6='server'
uci set dhcp.lan.ra_default='1'
```

`ip6assign` 从私有前缀中给该接口分配一个 `/64`。`ra` 和 `dhcpv6` 让 OpenWRT 向客户端通告它。`ra_default '1'` 让客户端把 OpenWRT 当作 IPv6 网关，即使前缀是私有的；否则 OpenWRT 不会这样通告。

**4. 防火墙：放行 IPv6 入站，并在出站时转换**

把 `wan6` 放进与 `wan` 相同的区域，这样同一套转发规则也适用于它。如果你的区域拒绝入站，还需要放行 ICMPv6 和 DHCPv6，否则 IPv6 无法发现邻居和路由器。

使用 iptables 防火墙的镜像（fw3，Android 上需要它）通过一个小脚本添加 IPv6 NAT：

```bash
cat > /etc/firewall.nat6 <<'EOF'
ip6tables -t nat -C POSTROUTING -o eth0 -j MASQUERADE 2>/dev/null ||
	ip6tables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
EOF
uci set firewall.nat6=include
uci set firewall.nat6.path='/etc/firewall.nat6'
uci set firewall.nat6.reload='1'
```

使用 nftables 防火墙的镜像（fw4，Linux 宿主上 OpenWRT 的默认防火墙）只需在 `wan` 区域加一个选项：`option masq6 '1'`。

**5. 应用并检查**

```bash
uci commit
/etc/init.d/network reload
/etc/init.d/firewall restart
/etc/init.d/odhcpd restart
```

等待大约二十秒，然后：

```bash
ip -6 addr show eth0        # 一个 fd64:7370:: 地址
ip -6 route | grep default  # default via fe80::1 dev eth0
ping6 -c3 google.com
```

之后 LAN 上的客户端容器会从 OpenWRT 的私有前缀获得地址，并且可以 `ping6` 互联网。

需要了解的三点：

- **对于同时支持两种协议的站点，客户端仍然优先使用 IPv4。** 操作系统会把私有 IPv6 源地址排在 IPv4 之后，因此 IPv6 主要用于只有 IPv6 的目标。
- **同步你的隔离规则。** 如果防火墙禁止客户端访问 Droidspaces NAT 网络 `172.28.0.0/16`，请为 `fd64:7370::/48` 添加同样的规则并加上 `option family 'ipv6'`，否则客户端可以通过 IPv6 访问其他 NAT 容器。
- **在网关容器上使用 `--disable-ipv6` 会关闭以上全部功能**，在客户端容器上使用则只关闭该客户端的 IPv6。

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

| 特性 | NAT 模式 | 主机模式 | 无网络模式 | 网关模式 |
|---|---|---|---|---|
| 谁分配 IP？ | Droidspaces DHCP | Android（共享） | 无（仅 loopback） | OpenWRT dnsmasq |
| 谁做 NAT？ | Droidspaces iptables | Android | 不适用 | OpenWRT（通过 Droidspaces 为 OpenWRT 的 WAN 做 NAT） |
| 谁管理防火墙？ | Droidspaces | Android | 不适用 | OpenWRT |
| 谁管理 DNS？ | Droidspaces | Android | 无 | OpenWRT dnsmasq |
| 容器与宿主机网络隔离？ | 是 | 否 | 是 | 是 |
| 互联网访问？ | 是 | 是 | 否 | 是（通过网关容器） |
| IPv6？ | 支持，使用 NAT66（需要内核支持） | 取决于 Android | 仅回环 | 支持，需先配置 OpenWRT（见第 10 部分） |
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
| **DNS** | 将域名（`google.com`）转换为 IP 地址的系统 |
| **NAT** | 将一个公网 IP 在多个私有 IP 设备之间共享的技术 |
| **网桥** | 连接多个网络接口的虚拟（或物理）交换机 |
| **veth 对** | 一对像管道一样连接的虚拟网络接口——从一端进，从另一端出 |
| **网络命名空间** | Linux 网络栈的隔离副本——容器生活在自己的命名空间中 |
| **OpenWRT** | 一个为作为路由器/网关运行而设计的 Linux 发行版——运行 dnsmasq、netifd、防火墙 |
| **netifd** | OpenWRT 的网络接口守护进程——管理接口和 DHCP |
| **dnsmasq** | OpenWRT 使用的轻量级 DHCP 和 DNS 服务器 |
| **MASQUERADE** | 实现 NAT 的 Linux iptables 规则（改写源 IP） |
| **NAT66** | IPv6 的 NAT：私有 IPv6 地址在出站时被改写为上行链路的地址 |
| **ULA** | 以 `fd` 开头的私有 IPv6 前缀，相当于 IPv6 中的 `192.168.x.x` |
| **路由通告 (RA)** | IPv6 路由器发出的消息，设备据此自行配置地址和网关，取代了 DHCP 的大部分作用 |
| **委托 LAN** | Droidspaces 在网关模式中创建的网桥网络——策略由网关容器而非 Droidspaces 掌管 |
| **网段** | 由 `--gateway-net` 标识的一个隔离 LAN——每个网段获得自己的网桥和在网关容器内的独立接口 |
| **延迟挂接** | 网关的 LAN 侧 veth 仅在第一个客户端容器启动时创建，而非网关容器启动时创建 |
