"""Capabilities, seccomp, masks, mount propagation, the --privileged tags and --allow-sandboxing.

-P/--selinux-permissive is deliberately not here: on Linux it sets the host's SELinux to permissive.
"""

import unittest

from dstest import CAPS_DROPPED_NO_HW, Container, DsTest, cap_mask, ds, require_daemon


def setUpModule():
    require_daemon()


MASKED = ("/proc/sysrq-trigger", "/proc/kcore", "/proc/timer_list", "/proc/irq", "/sys/firmware",
          "/sys/kernel/security", "/sys/kernel/debug", "/sys/kernel/tracing", "/sys/block")


def caps(c):
    out = c.sh("grep -E '^(CapBnd|CapEff|Seccomp):' /proc/self/status")
    return dict(line.replace(":", "").split() for line in out.splitlines())


class DefaultPosture(DsTest):
    @classmethod
    def setUpClass(cls):
        cls.c = cls.shared_container("sec")

    def test_bounding_set_is_trimmed(self):
        d = caps(self.c)
        self.assertEqual(d["CapBnd"], cap_mask(CAPS_DROPPED_NO_HW))
        self.assertEqual(d["CapEff"], d["CapBnd"])

    def test_seccomp_filter_is_active(self):
        self.assertEqual(caps(self.c)["Seccomp"], "2")
        # adjtimex with no arguments only reads the kernel's time state, so it is a safe probe for the
        # EPERM rule. Never use `date -s` here: without the filter it would set the host clock.
        p = self.c.run("adjtimex", check=False)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Operation not permitted", p.stderr)

    def test_masks(self):
        mounts = self.c.mountinfo()
        for path in MASKED:
            self.assertIn(f" {path} ", mounts, f"{path} is not masked")
        self.assertIn("Operation not permitted", self.c.run("cat", "/proc/kcore", check=False).stderr)
        self.assertIn("Read-only file system", self.c.run("sh", "-c", "echo 1 > /proc/sysrq-trigger", check=False).stderr)
        self.assertIn("Read-only file system", self.c.run("sh", "-c", "echo 1 > /proc/sys/kernel/panic", check=False).stderr)

    def test_hostname_sysctl_stays_writable(self):
        self.assertEqual(self.c.sh("cat /proc/sys/kernel/hostname"), self.c.name)
        self.assertEqual(self.c.sh("echo 1 > /proc/sys/net/ipv4/ip_forward && echo ok"), "ok")

    def test_user_namespaces_are_blocked(self):
        self.assertNotEqual(self.c.run("unshare", "-Ur", "true", check=False).returncode, 0)

    def test_mounts_are_private(self):
        self.assertNotIn("shared:", self.c.mountinfo())

    def test_no_pristine_proc_without_sandboxing(self):
        names = set(self.c.sh("ls /run/droidspaces").split())
        self.assertIn("container.config", names)
        self.assertNotIn("proc", names)
        self.assertNotIn("sys", names)

    def test_virtualized_proc_files(self):
        mounts = self.c.mountinfo()
        self.assertIn(" /proc/uptime ", mounts)
        self.assertIn(" /proc/loadavg ", mounts)
        self.assertLess(float(self.c.sh("cut -d' ' -f1 /proc/uptime")), 3600, "uptime inside is the container's, not the host's")


class Privileged(DsTest):
    def test_nomask(self):
        c = self.container("nomask", "--privileged=nomask")
        mounts = c.mountinfo()
        for path in MASKED:
            self.assertNotIn(f" {path} ", mounts, f"{path} still masked")
        d = caps(c)
        self.assertEqual((d["CapBnd"], d["Seccomp"]), (cap_mask(CAPS_DROPPED_NO_HW), "2"))
        self.assertEqual(c.info()["privileged_mode"], "nomask")

    def test_nocaps(self):
        c = self.container("nocaps", "--privileged=nocaps")
        d = caps(c)
        self.assertEqual((d["CapBnd"], d["Seccomp"]), (cap_mask(), "2"))
        self.assertIn(" /proc/sysrq-trigger ", c.mountinfo())

    def test_noseccomp(self):
        c = self.container("noseccomp", "--privileged=noseccomp")
        d = caps(c)
        self.assertEqual((d["CapBnd"], d["Seccomp"]), (cap_mask(CAPS_DROPPED_NO_HW), "0"))
        self.assertEqual(c.run("adjtimex", check=False).returncode, 0, "read-only adjtimex works without the filter")

    def test_shared(self):
        c = self.container("shared", "--privileged=shared")
        self.assertIn("shared:", c.mountinfo())
        self.assertEqual(caps(c)["Seccomp"], "2")

    def test_full(self):
        c = self.container("full", "--privileged=full")
        d = caps(c)
        self.assertEqual((d["CapBnd"], d["Seccomp"]), (cap_mask(), "0"))
        mounts = c.mountinfo()
        self.assertIn("shared:", mounts)
        self.assertNotIn(" /proc/sysrq-trigger ", mounts)
        self.assertEqual(c.saved_config()["privileged"], "full")

    def test_two_tags(self):
        c = self.container("twotags", "--privileged=nocaps,noseccomp")
        d = caps(c)
        self.assertEqual((d["CapBnd"], d["Seccomp"]), (cap_mask(), "0"))
        self.assertIn(" /proc/sysrq-trigger ", c.mountinfo())

    def test_unknown_tag_is_refused(self):
        # The documented tags are nomask, nocaps, noseccomp, shared and full. A typo in a flag that
        # removes security barriers must fail closed, not start the container with a silent no-op.
        c = self.container("badtag", start=False)
        self.assertNotEqual(c.start("--privileged=bogus", expect=None).returncode, 0)
        self.assertIsNone(c.pid())

    def test_unknown_tag_in_a_config_file_leaves_the_container_confined(self):
        # Same rule for a hand-edited container.config: nothing is granted, the container still
        # boots, and the file keeps the value as written so the warning repeats until it is fixed.
        c = self.container("badtag-conf", start=False)
        self.addCleanup(c.cleanup)
        conf = c.work / "x.conf"
        conf.write_text(f"name={c.name}\nrootfs_path={c.rootfs}\nprivileged=nocaps,bogus\n")
        p = ds("-C", str(conf), "start", check=True)
        self.assertIn("Unknown privileged tag 'bogus'", p.stdout + p.stderr)
        d = caps(c)
        self.assertEqual((d["CapBnd"], d["Seccomp"]), (cap_mask(CAPS_DROPPED_NO_HW), "2"))
        self.assertEqual(c.saved_config()["privileged"], "nocaps,bogus")

    def test_a_neighbours_bad_config_is_its_own_business(self):
        # Every start, listing and stop reads other containers' config files: the static IP check, the
        # gateway wiring, the container table. A bad value in one of them used to be printed, and logged,
        # under whichever container was being worked on. The owner still hears about it, by -C and by -n.
        bad = Container("badtag-neighbour").prepare()
        self.addCleanup(bad.cleanup)
        conf = bad.work / "x.conf"
        conf.write_text(f"name={bad.name}\nrootfs_path={bad.rootfs}\nprivileged=nocaps,bogus\n")
        p = ds("-C", str(conf), "start", check=True)
        self.assertIn("Unknown privileged tag 'bogus'", p.stdout + p.stderr)
        bad.stop(expect=0)
        p = bad.start(rootfs=False, reset=False)  # the saved config, by name
        self.assertIn("Unknown privileged tag 'bogus'", p.stdout + p.stderr)
        self.assertIn("Unknown privileged tag 'bogus'", bad.log())

        good = Container("good-neighbour").prepare()
        self.addCleanup(good.cleanup)
        p = good.start()
        for what, text in (("start", p), ("show", ds("show", check=True)), ("stop", good.stop(expect=0))):
            self.assertNotIn("privileged tag", text.stdout + text.stderr, f"a neighbour's complaint in {what}")
        self.assertNotIn("privileged tag", good.log())

    def test_unknown_tag_grants_nothing(self):
        c = self.container("badtag2", "--privileged=bogus", expect=None)
        if c.pid() is None:
            return  # refused, which is what test_unknown_tag_is_refused wants
        d = caps(c)
        self.assertEqual((d["CapBnd"], d["Seccomp"]), (cap_mask(CAPS_DROPPED_NO_HW), "2"))
        self.assertIn(" /proc/sysrq-trigger ", c.mountinfo())


class Sandboxing(DsTest):
    def test_allow_sandboxing(self):
        c = self.container("sandbox", "--allow-sandboxing")
        mounts = c.mountinfo()
        self.assertIn(" /run/droidspaces/proc ", mounts)
        self.assertIn(" /run/droidspaces/sys ", mounts)
        self.assertEqual(c.run("unshare", "-Ur", "true", check=False).returncode, 0)
        dropped = tuple(n for n in CAPS_DROPPED_NO_HW if n != 19)
        self.assertEqual(caps(c)["CapBnd"], cap_mask(dropped), "CAP_SYS_PTRACE must stay for sandboxes")
        self.assertEqual(c.info()["sandboxing_allowed"], 1)
        self.assertEqual(c.saved_config()["allow_sandboxing"], "1")
