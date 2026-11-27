"""Commands that need no container: version, help, docs, mode, check, scan, argument validation."""

import os
import re
import subprocess
import unittest

from dstest import DS, Container, DsTest, ds, ds_json, require_daemon, tree_version


def setUpModule():
    require_daemon()


class Cli(DsTest):
    def test_version_matches_tree(self):
        # The daemon serves every command with the installed binary, so a stale install fails here first.
        self.assertEqual(ds("version", check=True).stdout.strip(), "v" + tree_version())

    def test_help_lists_every_command(self):
        out = ds("help", check=True).stdout
        self.assertTrue(out.startswith("Droidspaces v"), out[:80])
        self.assertIn("Usage: droidspaces [options] <command> [args]", out)
        for cmd in ("start", "stop", "restart", "enter", "run", "info", "pid", "show", "scan", "check", "docs", "help", "version", "daemon"):
            self.assertRegex(out, re.compile(rf"^\s+{cmd}\b", re.M), f"{cmd} missing from help")
        for flag in ("--rootfs", "--rootfs-img", "--name", "--hostname", "--conf", "--net", "--gateway", "--macvlan-parent",
                     "--nat-ip", "--upstream", "--port", "--dns", "--disable-ipv6", "--hw-access", "--gpu", "--x11",
                     "--memory", "--allow-swap", "--cpus", "--pids-limit", "--privileged", "--allow-sandboxing",
                     "--allow-vts", "--foreground", "--init", "--user", "--stdin", "--env", "--bind", "--reset", "--json"):
            self.assertIn(flag, out, f"{flag} missing from help")

    def test_docs_print_every_page_without_a_tty(self):
        out = ds("docs", check=True).stdout
        lines = [l for l in out.splitlines() if l.strip()]
        self.assertGreater(len(lines), 100)
        for topic in ("--rootfs", "--net", "--privileged", "--bind"):
            self.assertIn(topic, out)

    def test_mode_is_daemon(self):
        self.assertEqual(ds("mode", check=True).stdout.strip(), "daemon")

    def test_check_json(self):
        # check is never proxied, so it runs as us: root=0 is right and requirements_met follows it.
        d = ds_json("check")
        self.assertEqual(d["kernel_version"], 1)
        self.assertIn("requirements_met", d)
        self.assertIn(tree_version(), d["version"])
        for probe in ("pid_ns", "mnt_ns", "net_ns", "cgroup2", "veth", "bridge", "seccomp", "overlayfs"):
            self.assertIn(d[probe], (0, 1), probe)

    def test_check_human(self):
        p = ds("check", check=True)
        self.assertTrue(p.stdout.strip())

    def test_show_json_shape(self):
        d = ds_json("show")
        self.assertIsInstance(d["total"], int)
        self.assertIsInstance(d["ram_total_kb"], int)
        self.assertIsInstance(d["running"], list)

    def test_show_human_with_nothing_running(self):
        p = ds("show", check=True)
        self.assertTrue(p.stdout.strip())

    def test_unknown_command(self):
        p = ds("frobnicate")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Unknown command", p.stdout + p.stderr)

    def test_unknown_option_is_rejected(self):
        # A misspelt flag must not be dropped on the floor.
        p = ds("--bogus-option", "version")
        self.assertNotEqual(p.returncode, 0)

    def test_daemon_needs_root(self):
        p = ds("daemon")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Root privileges required", p.stdout + p.stderr)

    def test_direct_mode_check_works_without_daemon(self):
        env = dict(os.environ, DS_NO_PROXY="1")
        p = subprocess.run([DS, "check"], capture_output=True, text=True, env=env, timeout=60)
        self.assertEqual(p.returncode, 0, p.stderr)

    def test_scan_runs_without_containers(self):
        p = ds("scan", check=True)
        self.assertIn("Scanning system for untracked Droidspaces containers", p.stdout)


class StartValidation(DsTest):
    """start must refuse bad input before anything is created."""

    def setUp(self):
        self.c = Container("validation").prepare()
        self.addCleanup(self.c.cleanup)

    def test_name_is_mandatory(self):
        p = ds("-r", str(self.c.rootfs), "start")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Container name is mandatory", p.stdout + p.stderr)

    def test_invalid_name(self):
        for bad in ("../x", "a/b", "..", "x\ty"):
            p = ds("-n", bad, "-r", str(self.c.rootfs), "start")
            self.assertNotEqual(p.returncode, 0, bad)
            self.assertIn("Invalid container name", p.stdout + p.stderr)

    def test_missing_rootfs(self):
        p = ds("-n", self.c.name, "-r", "/nonexistent/rootfs", "start")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Rootfs directory not found", p.stdout + p.stderr)

    def test_rootfs_is_required(self):
        p = ds("-n", self.c.name, "start")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("No rootfs target specified", p.stdout + p.stderr)

    def test_init_path_must_be_absolute(self):
        p = self.c.start("--init=sbin/init", expect=None)
        self.assertNotEqual(p.returncode, 0)
        self.assertIsNone(self.c.pid())

    def test_bind_dest_must_be_absolute(self):
        p = self.c.start("-B", f"{self.c.work}:relative", expect=None)
        self.assertNotEqual(p.returncode, 0)
        self.assertIsNone(self.c.pid())

    def test_start_rejects_a_name_list(self):
        p = ds("-n", "a,b", "-r", str(self.c.rootfs), "start")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("does not support multiple containers", p.stdout + p.stderr)
