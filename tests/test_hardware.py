"""Hardware access, GPU nodes, VT masking, X11 bridging, and the Android-only flags on a Linux host."""

import glob
import os
import unittest

from dstest import CAPS_DROPPED_ALWAYS, DsTest, cap_mask, require_daemon


def setUpModule():
    require_daemon()


def dev_major(c, node):
    """Major number of the device behind `node`, symlinks followed. Host VTs are major 4."""
    return int(c.sh(f"stat -L -c %t {node}"), 16)


class HardwareAccess(DsTest):
    def test_default_dev_is_an_isolated_tmpfs(self):
        c = self.container("notmpfs")
        self.assertEqual(c.fstype("/dev"), "tmpfs")
        self.assertNotEqual(dev_major(c, "/dev/tty1"), 4, "host VT must not be reachable")
        self.assertEqual(c.info()["hw_access"], "none")
        self.assertEqual(c.sh("[ -e /dev/dri ] || echo none"), "none")

    def test_hw_access_exposes_devtmpfs_and_masks_vts(self):
        c = self.container("hw", "-H")
        self.assertEqual(c.fstype("/dev"), "devtmpfs")
        for vt in range(1, 7):
            self.assertNotEqual(dev_major(c, f"/dev/tty{vt}"), 4, f"tty{vt} must be hidden")
        self.assertEqual(dev_major(c, "/dev/tty7"), 4, "only tty1-6 are hidden")
        self.assertEqual(c.sh("grep ^CapBnd /proc/self/status | cut -f2"), cap_mask(CAPS_DROPPED_ALWAYS))
        self.assertIn(" /proc/kcore ", c.mountinfo(), "universal masks stay")
        d = c.info()
        self.assertEqual((d["hw_access"], d["vts_allowed"]), ("full", 0))
        self.assertEqual(c.saved_config()["enable_hw_access"], "1")

    def test_allow_vts(self):
        c = self.container("vts", "-H", "--allow-vts")
        self.assertEqual(dev_major(c, "/dev/tty1"), 4)
        self.assertEqual(c.info()["vts_allowed"], 1)

    def test_gpu_nodes(self):
        if not glob.glob("/dev/dri/renderD*"):
            self.skipTest("host has no /dev/dri/renderD* node")
        c = self.container("gpu", "--gpu")
        # Features.md: only the render nodes are mirrored, owned by the droidspaces-gpu group
        render = c.sh("ls /dev/dri/ 2>/dev/null")
        self.assertIn("renderD", render)
        self.assertNotIn("card", render, "--gpu copies render nodes only")
        # Root opens the nodes as their owner; the group has no members, other users are added by hand
        self.assertEqual(c.sh("stat -c '%U %G %a' /dev/dri/renderD* | head -1"), "root droidspaces-gpu 660")
        self.assertEqual(c.sh("grep -c '^droidspaces-gpu:x:786:$' /etc/group"), "1")
        self.assertEqual(c.info()["hw_access"], "GPU")
        self.assertEqual(c.saved_config()["enable_gpu_mode"], "1")

    def test_x11_socket_is_bridged(self):
        if not os.path.exists("/tmp/.X11-unix/X0"):
            self.skipTest("host has no X11 socket at /tmp/.X11-unix/X0")
        c = self.container("x11", "-X")
        self.assertEqual(c.sh("[ -S /tmp/.X11-unix/X0 ] && echo sock"), "sock")
        self.assertEqual(c.sh("stat -c %a /tmp/.X11-unix"), "1777")
        self.assertEqual(c.info()["x11"], 1)
        self.assertEqual(c.saved_config()["enable_x11"], "1")

    def test_x11_is_off_by_default(self):
        c = self.container("nox11")
        self.assertEqual(c.sh("ls /tmp/.X11-unix 2>&1 | head -c 0; [ -e /tmp/.X11-unix/X0 ] || echo none"), "none")
        self.assertEqual(c.info()["x11"], 0)


class AndroidOnlyFlags(DsTest):
    def test_android_flags_warn_and_are_skipped_on_linux(self):
        c = self.container("android", start=False)
        p = c.start("--enable-android-storage", "--tx11-flags=-xstartup", "--virgl", "--virgl-flags=-v",
                    "--pulse-audio", "--anland")
        out = p.stdout + p.stderr
        self.assertIn("--enable-android-storage is only supported on Android hosts", out)
        for flag in ("--tx11-flags", "--virgl", "--pulse-audio", "--anland"):
            self.assertIn(f"{flag} is only applicable on Android. Skipping.", out)
        self.assertIsNotNone(c.pid())
        self.assertEqual(c.sh("[ -d /sdcard ] || echo none"), "none")
        saved = c.saved_config()
        for key in ("enable_virgl", "enable_pulseaudio", "enable_anland", "tx11_extra_flags"):
            self.assertNotIn(key, saved, f"{key} must not be saved on Linux")

    def test_android_storage_short_flag(self):
        c = self.container("android-short", start=False)
        p = c.start("-S")
        self.assertIn("only supported on Android hosts", p.stdout + p.stderr)


class DeviceNodes(DsTest):
    def test_dev_null_is_writable_by_everyone(self):
        # getty chmods its tty to 0620; when ttyN was a symlink that landed on /dev/null itself.
        c = self.container("devnull")
        self.assertEqual(c.sh("echo x > /dev/null && echo ok", user="tester"), "ok")

    def test_basic_nodes_exist(self):
        c = self.container("devnodes")
        for node in ("null", "zero", "full", "random", "urandom", "tty", "console", "ptmx", "net/tun"):
            self.assertEqual(c.sh(f"[ -c /dev/{node} ] && echo c"), "c", node)
        self.assertEqual(c.sh("ls /dev/pts >/dev/null && echo pts"), "pts")
