"""CPU pinning through the host's cpuset controller, including hybrid/v1 hosts."""

import os
import re
import shutil
import unittest
from pathlib import Path

from dstest import DS, DsTest, ds, ds_json, require_daemon


def cpu_ids(value):
    result = set()
    for item in value.split(","):
        first, _, last = item.partition("-")
        result.update(range(int(first), int(last or first) + 1))
    return result


ALLOWED = "awk '/^Cpus_allowed_list:/ {print $2}' /proc/self/status"


class CpuSetSyntax(unittest.TestCase):
    def test_invalid_lists_are_rejected(self):
        for value in ("-1", "3-1", "1,", ",1", "1,,2", "1-", "1-2-3", "+1",
                      "1 2", "1024", "99999999999999999999999", "0\n1", "0," * 600):
            with self.subTest(value=value):
                p = ds(f"--cpuset-cpus={value}", "help")
                self.assertEqual(p.returncode, 1)
                self.assertIn("--cpuset-cpus", p.stderr)

    def test_lists_ranges_and_clearing_are_accepted(self):
        for value in ("0", "0-3,6", "4,2,2", "0-0", "1023", ""):
            with self.subTest(value=value):
                self.assertEqual(ds(f"--cpuset-cpus={value}", "help").returncode, 0)

    @classmethod
    def setUpClass(cls):
        if shutil.which(DS) is None:
            raise unittest.SkipTest(f"{DS} not found")


class CpuPinning(DsTest):
    def container(self, tag, *flags, **kwargs):
        return super().container(tag, "--net=none", *flags, **kwargs)

    @classmethod
    def setUpClass(cls):
        require_daemon()
        if not ds_json("check").get("cpuset"):
            raise unittest.SkipTest("cpuset controller unavailable")
        cls.cpus = sorted(os.sched_getaffinity(0))
        cls.selected = str(cls.cpus[-1])

    def test_init_children_run_and_enter_are_pinned(self):
        c = self.container("cpuset-sessions", f"--cpuset-cpus={self.selected}")
        self.assertEqual(cpu_ids(c.info()["cpuset_cpus"]), {int(self.selected)})
        self.assertEqual(c.saved_config()["cpuset_cpus"], self.selected)
        self.assertEqual(cpu_ids(c.sh(ALLOWED)), {int(self.selected)})
        self.assertEqual(cpu_ids(c.sh(ALLOWED.replace("/proc/self/", "/proc/1/"))), {int(self.selected)})
        self.assertEqual(cpu_ids(c.sh(f"({ALLOWED})")), {int(self.selected)})
        out, code = c.enter("awk '/^Cpus_allowed_list:/ {print \"PINNED=\" $2}' /proc/self/status")
        self.assertEqual(code, 0)
        self.assertEqual(re.findall(r"^PINNED=(\S+)$", out, re.M), [self.selected])

    def test_taskset_cannot_widen_beyond_the_cpuset(self):
        c = self.container("cpuset-affinity", f"--cpuset-cpus={self.selected}")
        if c.run("sh", "-c", "command -v taskset", check=False).returncode:
            self.skipTest("test rootfs needs taskset from util-linux")
        mask = (1 << (self.cpus[-1] + 1)) - 1
        self.assertEqual(cpu_ids(c.sh(f"taskset -p {mask:x} $$ >/dev/null && {ALLOWED}")), {int(self.selected)})

    def test_restart_changes_and_clears_pinning(self):
        c = self.container("cpuset-restart", f"--cpuset-cpus={self.selected}")
        other = str(self.cpus[0])
        c.restart(f"--cpuset-cpus={other}")
        self.assertEqual(cpu_ids(c.sh(ALLOWED)), {int(other)})
        c.restart("--cpuset-cpus=")
        self.assertEqual(c.info()["cpuset_cpus"], "")
        self.assertNotIn("cpuset_cpus", c.saved_config())

    def test_quota_and_pinning_together(self):
        if not ds_json("check").get("cpu_limit"):
            self.skipTest("CPU quotas unavailable")
        selected = f"{self.cpus[0]},{self.cpus[-1]}"
        c = self.container("cpuset-quota", f"--cpuset-cpus={selected}", "--cpus=0.5")
        self.assertEqual(c.info()["cpu_quota"], 50000)
        self.assertEqual(cpu_ids(c.sh(ALLOWED)), {self.cpus[0], self.cpus[-1]})

    def test_reset_removes_pinning(self):
        c = self.container("cpuset-reset", f"--cpuset-cpus={self.selected}")
        c.restart("--reset", "--net=none")
        self.assertEqual(c.info()["cpuset_cpus"], "")
        self.assertNotIn("cpuset_cpus", c.saved_config())

    def test_cpu_absent_from_the_host_is_rejected(self):
        c = self.container("cpuset-absent", start=False)
        absent = max(cpu_ids(Path("/sys/devices/system/cpu/present").read_text())) + 1
        self.assertNotEqual(c.start(f"--cpuset-cpus={absent}", expect=None).returncode, 0)

    def test_invalid_saved_pinning_does_not_boot_unpinned(self):
        c = self.container("cpuset-invalid", start=False)
        config = c.work / "container.config"
        config.write_text(f"name={c.name}\nrootfs_path={c.rootfs}\ncpuset_cpus=1,,2\n")
        self.assertNotEqual(ds("--config", str(config), "start").returncode, 0)

    def test_v1_container_is_pinned(self):
        c = self.container("cpuset-v1", "--force-cgroupv1", f"--cpuset-cpus={self.selected}")
        self.assertEqual(cpu_ids(c.sh(ALLOWED)), {int(self.selected)})
