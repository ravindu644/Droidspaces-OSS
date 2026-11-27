"""Resource limits through cgroup v2: memory, swap, cpus, pids, and the cgroup v1 override."""

import json
import os
import unittest
from pathlib import Path

from dstest import DsTest, ds, ds_json, require_daemon


def setUpModule():
    require_daemon()
    if not Path("/sys/fs/cgroup/cgroup.controllers").exists():
        raise unittest.SkipTest("host is not on cgroup v2; these tests read the unified hierarchy")


class Limits(DsTest):
    def test_memory(self):
        c = self.container("mem", "--memory=64M")
        self.assertEqual(c.cgroup("memory.max"), "67108864")
        self.assertEqual(c.sh("cat /sys/fs/cgroup/memory.max"), "67108864")
        self.assertEqual(c.info()["memory_limit"], 67108864)
        entry = [e for e in ds_json("show")["running"] if e["name"] == c.name][0]
        self.assertEqual(entry["ram_limit_kb"], 65536)
        self.assertEqual(c.saved_config()["memory_limit"], "67108864")

    def test_swap_is_off_with_a_memory_limit(self):
        c = self.container("noswap", "--memory=64M")
        self.assertEqual(c.cgroup("memory.swap.max"), "0")
        self.assertEqual(c.info()["swap_allowed"], 0)

    def test_allow_swap(self):
        c = self.container("swap", "--memory=64M", "--allow-swap")
        self.assertEqual(c.cgroup("memory.swap.max"), "max")
        self.assertEqual(c.info()["swap_allowed"], 1)

    def test_cpus(self):
        c = self.container("cpus", "--cpus=1.5")
        self.assertEqual(c.cgroup("cpu.max"), "150000 100000")
        d = c.info()
        self.assertEqual((d["cpu_quota"], d["cpu_period"]), (150000, 100000))
        entry = [e for e in ds_json("show")["running"] if e["name"] == c.name][0]
        # The app divides cpu_permill by cpu_limit_permill for its usage bar, so both are
        # shares of the whole machine: 1.5 cores out of N.
        self.assertEqual(entry["cpu_limit_permill"], int(1.5 * 1000 / os.cpu_count()))

    def test_pids_limit(self):
        c = self.container("pids", "--pids-limit=50")
        self.assertEqual(c.cgroup("pids.max"), "50")
        self.assertEqual(c.sh("cat /sys/fs/cgroup/pids.max"), "50")
        self.assertEqual(c.info()["pids_limit"], 50)

    def test_minimums_are_enforced(self):
        c = self.container("limits-bad", start=False)
        self.assertEqual(c.start("--memory=1M", expect=None).returncode, 1)
        self.assertEqual(c.start("--pids-limit=5", expect=None).returncode, 1)
        self.assertIsNone(c.pid())

    def test_no_limits_by_default(self):
        c = self.container("nolimit")
        # No file means the controller is off there, which is no limit as well
        self.assertEqual(c.cgroup("memory.max", missing="max"), "max")
        self.assertEqual(c.cgroup("pids.max", missing="max"), "max")
        self.assertEqual(c.cgroup("cpu.max", missing="max 100000"), "max 100000")

    def test_cgroup_is_removed_on_stop(self):
        c = self.container("cgrm")
        path = Path("/sys/fs/cgroup/droidspaces") / c.name
        self.assertTrue(path.is_dir())
        c.stop()
        self.assertFalse(path.exists())

    def test_force_cgroupv1_is_accepted_on_a_v2_host(self):
        c = self.container("cgv1", start=False)
        p = c.start("--force-cgroupv1")
        self.assertIn("Using legacy Cgroup V1 hierarchy", p.stdout + p.stderr)
        self.assertIsNotNone(c.pid())
        self.assertEqual(c.info()["force_cgroup_v1"], 1)

    def test_info_json_is_pure_json_with_limits_set(self):
        c = self.container("json-limits", "--memory=64M")
        json.loads(ds("--json", "-n", c.name, "info", check=True).stdout)
