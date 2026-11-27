"""Networking: nat, fixed IP, DNS, IPv6, port forwards, upstream pinning, host, none, gateway, macvlan."""

import os
import subprocess
import time
import unittest
import urllib.request

from dstest import DsTest, ds, has_internet, require_daemon


def setUpModule():
    require_daemon()


def host_links():
    out = subprocess.run(["ip", "-o", "link"], capture_output=True, text=True, check=True).stdout
    return [l.split(":")[1].strip().split("@")[0] for l in out.splitlines()]


def default_iface():
    out = subprocess.run(["ip", "route", "show", "default"], capture_output=True, text=True).stdout.split()
    return out[out.index("dev") + 1] if "dev" in out else None


def http_get(url, timeout=4, retries=10):
    """GET with retries: the busybox nc server below serves one connection at a time and respawns between them."""
    for _ in range(retries):
        try:
            with urllib.request.urlopen(url, timeout=timeout) as r:
                return r.read().decode()
        except Exception as e:
            err = f"ERROR {e}"
            time.sleep(0.5)
    return err


SERVE = ("(while :; do printf 'HTTP/1.0 200 OK\\r\\nContent-Length: 6\\r\\n\\r\\nSERVED' | nc -l -p 80; done)"
         " >/dev/null 2>&1 & sleep 0.5")
WGET = "for i in 1 2 3 4 5 6 7 8; do wget -q -O- -T 4 {url} && exit 0; sleep 0.5; done; echo NO"


class Nat(DsTest):
    def test_nat_is_the_default(self):
        c = self.container("nat")
        d = c.info()
        self.assertEqual(d["networking_mode"], "NAT")
        self.assertTrue(d["nat_ip"].startswith("172.28."), d["nat_ip"])
        self.assertIn("eth0", c.links())
        # Droidspaces owns the resolver until a DHCP client inside rewrites the file
        self.assertEqual(c.sh("readlink /etc/resolv.conf || echo not-a-symlink"), "/run/droidspaces/resolv.conf")
        self.assertEqual(c.sh("cat /etc/resolv.conf"),
                         "nameserver 1.1.1.1\nnameserver 8.8.8.8\nnameserver 2606:4700:4700::1111")
        self.assertEqual(c.dhcp(), d["nat_ip"])
        self.assertIn("default via 172.28.0.1 dev eth0", c.sh("ip route"))
        self.assertIn("ds-br0", host_links())
        self.assertEqual(c.sh("ping -c1 -W2 172.28.0.1 >/dev/null && echo ok"), "ok")
        # The lease carries the same resolvers (DHCPv4 cannot carry the IPv6 one)
        self.assertEqual(c.sh("cat /etc/resolv.conf").splitlines()[:2], ["nameserver 1.1.1.1", "nameserver 8.8.8.8"])

    def test_fixed_nat_ip(self):
        c = self.container("natip", "--nat-ip=172.28.200.200")
        self.assertEqual(c.info()["nat_ip"], "172.28.200.200")
        self.assertEqual(c.dhcp(), "172.28.200.200")
        self.assertEqual(c.saved_config()["static_nat_ip"], "172.28.200.200")

    def test_nat_ip_outside_the_subnet_is_refused(self):
        c = self.container("natip-bad", start=False)
        self.assertEqual(c.start("--nat-ip=10.0.0.5", expect=None).returncode, 1)
        self.assertIsNone(c.pid())

    def test_dns(self):
        c = self.container("dns", "--dns=9.9.9.9,1.0.0.1")
        self.assertEqual(c.sh("cat /etc/resolv.conf"), "nameserver 9.9.9.9\nnameserver 1.0.0.1")
        self.assertEqual(c.info()["dns_servers"], "9.9.9.9,1.0.0.1")

    def test_disable_ipv6(self):
        c = self.container("noipv6", "--disable-ipv6")
        self.assertEqual(c.sh("cat /proc/sys/net/ipv6/conf/all/disable_ipv6 /proc/sys/net/ipv6/conf/default/disable_ipv6"), "1\n1")
        c.dhcp()
        self.assertEqual(c.sh("ip -6 -o addr show eth0"), "")
        self.assertEqual(c.info()["disable_ipv6"], 1)
        self.assertEqual(c.saved_config()["disable_ipv6"], "1")

    def test_disable_ipv6_short_flag(self):
        c = self.container("noipv6-short", "-I")
        self.assertEqual(c.sh("cat /proc/sys/net/ipv6/conf/all/disable_ipv6"), "1")

    def test_ipv6_is_on_by_default(self):
        c = self.container("ipv6")
        c.dhcp()
        self.assertIn("inet6", c.sh("ip -6 -o addr show eth0"))
        self.assertTrue(c.info()["ip6"])

    def test_port_forward_reaches_the_container(self):
        c = self.container("port", "--nat-ip=172.28.200.201", "--port=18080:80")
        c.dhcp()
        c.sh(SERVE)
        self.assertEqual(http_get("http://172.28.200.201/"), "SERVED", "direct access to the container")
        self.assertEqual(http_get("http://127.0.0.1:18080/"), "SERVED", "OUTPUT-chain DNAT from localhost")
        peer = self.container("port-peer")
        peer.dhcp()
        self.assertEqual(peer.sh(WGET.format(url="http://172.28.0.1:18080/")), "SERVED",
                         "PREROUTING DNAT from another host interface")

    def test_port_syntax_variants_are_saved(self):
        c = self.container("ports", "--port=22", "--port=80:8080/tcp", "--port=1000-1001:2000-2001/udp")
        self.assertEqual(c.info()["port_forwards"], "22:22/tcp,80:8080/tcp,1000-1001:2000-2001/udp")
        self.assertEqual(c.saved_config()["port_forwards"], "22:22/tcp,80:8080/tcp,1000-1001:2000-2001/udp")

    def test_port_is_dropped_outside_nat(self):
        c = self.container("port-none", start=False)
        p = c.start("--net=none", "--port=22")
        self.assertIn("--port is only valid with --net=nat", p.stdout + p.stderr)
        self.assertEqual(c.info()["port_forwards"], "")

    def test_upstream_pin(self):
        iface = default_iface()
        if not iface:
            self.skipTest("host has no default route")
        c = self.container("upstream", f"--upstream={iface}")
        self.assertEqual(c.info()["upstream_interfaces"], iface)
        self.assertEqual(c.saved_config()["upstream_interfaces"], iface)
        if has_internet():
            c.dhcp()
            self.assertEqual(c.sh("wget -q -O /dev/null --timeout=5 http://1.1.1.1/ && echo ok"), "ok")

    def test_internet_through_nat(self):
        if not has_internet():
            self.skipTest("host has no internet")
        c = self.container("internet")
        c.dhcp()
        self.assertEqual(c.sh("wget -q -O /dev/null --timeout=5 http://1.1.1.1/ && echo ok"), "ok")
        self.assertEqual(c.sh("wget -q -O /dev/null --timeout=8 http://one.one.one.one/ && echo dns-ok"), "dns-ok")


class HostAndNone(DsTest):
    def test_host_mode_shares_the_host_namespace(self):
        c = self.container("host", "--net=host")
        self.assertEqual(c.info()["networking_mode"], "host")
        self.assertTrue(set(host_links()) <= set(c.links()), (host_links(), c.links()))

    def test_none_mode_has_only_loopback(self):
        c = self.container("none", "--net=none")
        self.assertEqual(c.info()["networking_mode"], "none")
        self.assertEqual(c.links(), ["lo"])
        self.assertEqual(c.sh("ping -c1 -W1 127.0.0.1 >/dev/null && echo ok"), "ok")


class Gateway(DsTest):
    def test_gateway_mode_wires_a_lan_between_two_containers(self):
        gw = self.container("gw")
        client = self.container("gc", "--net=gateway", f"--gateway={gw.name}")
        self.assertIn("ds-lan", host_links())
        self.assertIn("eth1", gw.links())
        self.assertIn("eth0", client.links())
        d = client.info()
        self.assertEqual((d["networking_mode"], d["gateway_container"], d["gateway_net"], d["gateway_iface"]),
                         ("gateway", gw.name, "lan", "eth1"))
        gw.sh("ip addr add 10.9.0.1/24 dev eth1 && ip link set eth1 up")
        self.assertEqual(client.sh("ip addr add 10.9.0.2/24 dev eth0 && ip link set eth0 up && "
                                   "ping -c1 -W2 10.9.0.1 >/dev/null && echo ok"), "ok")
        # The LAN owns DNS: without --dns the client's resolv.conf is left to its DHCP client.
        self.assertIn("Leaving /etc/resolv.conf to the container's DHCP client", client.log())
        self.assertEqual(client.sh("[ -L /etc/resolv.conf ] && echo link || echo plain"), "plain")
        client.stop()
        self.assertIn("ds-lan", host_links(), "bridge stays while the gateway runs")
        gw.stop()
        self.assertNotIn("ds-lan", host_links(), "bridge goes with the gateway")

    def test_gateway_custom_names(self):
        gw = self.container("gw2")
        client = self.container("gc2", "--net=gateway", f"--gateway={gw.name}", "--gateway-net=tl",
                                "--gateway-iface=lan0", "--gateway-bridge=ds-tb", "--dns=9.9.9.9")
        self.assertIn("ds-tb", host_links())
        self.assertIn("lan0", gw.links())
        d = client.info()
        self.assertEqual((d["gateway_net"], d["gateway_iface"], d["gateway_bridge"]), ("tl", "lan0", "ds-tb"))
        # An explicit --dns overrides the LAN's DNS even in gateway mode
        self.assertEqual(client.sh("cat /etc/resolv.conf"), "nameserver 9.9.9.9")

    def test_gateway_mode_needs_a_gateway(self):
        c = self.container("gc-bad", start=False)
        self.assertEqual(c.start("--net=gateway", expect=None).returncode, 1)
        self.assertIsNone(c.pid())

    def test_stopping_the_gateway_leaves_the_client_running(self):
        gw = self.container("gw3")
        client = self.container("gc3", "--net=gateway", f"--gateway={gw.name}")
        gw.stop(expect=0)
        self.assertIsNotNone(client.pid())


class Macvlan(DsTest):
    def test_missing_parent_is_deferred_not_fatal(self):
        c = self.container("mv-missing", start=False)
        p = c.start("--net=macvlan", "--macvlan-parent=nosuch0")
        self.assertIn("nosuch0 is not present", p.stdout + p.stderr)
        self.assertIsNotNone(c.pid())
        d = c.info()
        self.assertEqual((d["networking_mode"], d["macvlan_parent"], d["macvlan_mode"]), ("macvlan", "nosuch0", "bridge"))
        self.assertNotIn("eth0", c.links())

    def test_parent_is_required(self):
        c = self.container("mv-noparent", start=False)
        self.assertNotEqual(c.start("--net=macvlan", expect=None).returncode, 0)
        self.assertIsNone(c.pid())

    def test_macvlan_on_a_real_parent(self):
        parent = os.environ.get("DS_TEST_MACVLAN_PARENT")
        if not parent:
            self.skipTest("set DS_TEST_MACVLAN_PARENT=<wired NIC> to run this")
        c = self.container("mv", "--net=macvlan", f"--macvlan-parent={parent}", "--macvlan-mode=private")
        self.assertIn("eth0", c.links())
        self.assertEqual(c.info()["macvlan_mode"], "private")
        self.assertEqual(c.saved_config()["macvlan_parent"], parent)
