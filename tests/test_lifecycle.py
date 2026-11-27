"""start, stop, restart, pid, show, info, enter, run, config persistence, init, env, binds, volatile, images, logs."""

import os
import pty
import select
import shutil
import subprocess
import time
import unittest
from pathlib import Path

from dstest import DS, WORKSPACE, Container, DsTest, ds, ds_json, foreign_running, require_daemon, strip_ansi


def setUpModule():
    require_daemon()


class Lifecycle(DsTest):
    def test_start_stop_pid_show(self):
        c = self.container("life")
        pid = c.pid()
        self.assertTrue(Path(f"/proc/{pid}").exists())
        entry = [e for e in ds_json("show")["running"] if e["name"] == c.name]
        self.assertEqual(len(entry), 1)
        self.assertEqual(entry[0]["pid"], pid)
        self.assertEqual(entry[0]["hostname"], c.name)
        self.assertTrue(entry[0]["os"])
        self.assertIn(c.name, ds("show", check=True).stdout)
        c.stop(expect=0)
        self.assertIsNone(c.pid())
        p = ds("-n", c.name, "pid")
        self.assertEqual(p.stdout.strip(), "NONE")
        self.assertNotEqual(p.returncode, 0)
        p = c.stop()
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("is not running or invalid", p.stdout + p.stderr)

    def test_second_start_is_refused(self):
        c = self.container("twice")
        p = c.start(reset=False, rootfs=False, expect=None)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("already in use", p.stdout + p.stderr)

    def test_restart_changes_pid_and_applies_cli_overrides(self):
        c = self.container("restart")
        before = c.pid()
        c.restart("--hostname=after-restart")
        self.assertNotEqual(c.pid(), before)
        self.assertEqual(c.sh("hostname"), "after-restart")

    def test_stop_several_with_a_comma_list(self):
        a, b = self.container("multi-a"), self.container("multi-b")
        ds("-n", f"{a.name},{b.name}", "stop", check=True)
        self.assertIsNone(a.pid())
        self.assertIsNone(b.pid())

    def test_name_can_be_left_out_with_one_container_running(self):
        if foreign_running():
            self.skipTest(f"other containers running: {foreign_running()}")
        c = self.container("solo")
        self.assertEqual(ds("run", "--", "hostname", check=True).stdout.strip(), c.name)
        self.assertEqual(int(ds("pid", check=True).stdout), c.pid())
        self.assertEqual(ds_json("info")["name"], c.name)

    def test_name_with_a_space(self):
        # The real name keeps its space; host-side folders use the sanitized form the app expects.
        c = self.container("sp ace")
        self.assertEqual(c.sh("hostname"), c.name)
        self.assertEqual(c.info()["name"], c.name)
        self.assertTrue((WORKSPACE / "Containers" / "dstest-sp-ace" / "container.config").exists())

    def test_hostname_defaults_to_name_and_flag_overrides(self):
        c = self.container("hostname")
        self.assertEqual(c.sh("hostname"), c.name)
        c.stop()
        c.start("-h", "custom-host")
        self.assertEqual(c.sh("hostname"), "custom-host")
        self.assertEqual(c.sh("cat /etc/hostname"), "custom-host")
        self.assertIn("custom-host", c.sh("cat /etc/hosts"))
        self.assertEqual(c.info()["hostname"], "custom-host")

    def test_saved_config_is_reused_by_a_bare_start(self):
        c = self.container("saved", start=False)
        (c.work / "share").mkdir()
        (c.work / "share/f").write_text("hi\n")
        c.start("-h", "saved-host", "-B", f"{c.work}/share:/mnt/share:ro", "--dns=9.9.9.9")
        self.assertTrue((c.work / "container.config").exists(), "a -r start writes the config next to the rootfs")
        c.stop()
        c.start(rootfs=False, reset=False)
        self.assertEqual(c.sh("hostname; cat /mnt/share/f; cat /etc/resolv.conf"), "saved-host\nhi\nnameserver 9.9.9.9")

    def test_reset_restores_defaults(self):
        c = self.container("reset", "-h", "custom", "--dns=9.9.9.9")
        c.stop()
        c.start(rootfs=False, reset=True)
        self.assertEqual(c.sh("hostname"), c.name)
        self.assertNotIn("9.9.9.9", c.sh("cat /etc/resolv.conf"))
        self.assertEqual(c.saved_config()["hostname"], c.name)

    def test_config_file_flag(self):
        c = Container("conf").prepare()
        self.addCleanup(c.cleanup)
        conf = c.work / "x.conf"
        conf.write_text(f"# comment\nname={c.name}\nrootfs_path={c.rootfs}\nhostname=from-conf\n")
        ds("-C", str(conf), "start", check=True)
        self.assertEqual(c.sh("hostname"), "from-conf")
        self.assertIn(f"name={c.name}", conf.read_text())

    def test_config_is_visible_inside(self):
        c = self.container("inside")
        self.assertIn(f"name={c.name}", c.sh("cat /run/droidspaces/container.config"))
        self.assertIn(ds("version", check=True).stdout.strip().lstrip("v"), c.sh("cat /run/droidspaces/version"))


class Run(DsTest):
    @classmethod
    def setUpClass(cls):
        cls.c = cls.shared_container("run")

    def test_exit_code_passes_through(self):
        self.assertEqual(self.c.run("sh", "-c", "exit 7", check=False).returncode, 7)
        self.assertEqual(self.c.run("/no/such/binary", check=False).returncode, 127)

    def test_single_argument_goes_to_sh(self):
        self.assertEqual(self.c.run("echo a | tr a b", check=True).stdout.strip(), "b")

    def test_stdin_only_with_flag(self):
        self.assertEqual(self.c.run("cat", stdin="piped\n").stdout, "piped\n")
        self.assertEqual(self.c.run("cat").stdout, "")

    def test_stdout_and_stderr_are_separate(self):
        p = self.c.run("sh", "-c", "echo out; echo err >&2")
        self.assertEqual((p.stdout.strip(), p.stderr.strip()), ("out", "err"))

    def test_as_user(self):
        self.assertEqual(self.c.run("id", "-un", user="tester").stdout.strip(), "tester")
        self.assertEqual(self.c.sh("echo $HOME; pwd", user="tester"), "/home/tester\n/home/tester")

    def test_environment_defaults(self):
        self.assertEqual(self.c.sh("echo $container; echo $HOME; pwd"), "droidspaces\n/root\n/")

    def test_caller_umask_reaches_the_command(self):
        # The daemon used to hand every command its own umask 0, so everything
        # created through run was world-writable.
        for mask in (0o022, 0o077):
            old = os.umask(mask)
            try:
                self.assertEqual(self.c.sh("umask"), f"{mask:04o}")
            finally:
                os.umask(old)

    def test_command_arguments_are_opaque(self):
        # "Anything after run belongs to the command", the daemon's recursion guard included.
        self.assertEqual(self.c.run("echo", "daemon", "client").stdout.strip(), "daemon client")
        # Our path options mean nothing to the command: `ffmpeg -i x` once
        # reached the container as `ffmpeg -i //x`.
        self.assertEqual(self.c.run("printf", "%s ", "-i", "x", "-E", "y", "-C", "z").stdout,
                         "-i x -E y -C z ")
        # The same without the -- fence, and the =value and bind forms.
        p = ds("-n", self.c.name, "run", "printf", "%s ", "-B", "/a:/b", "--rootfs=x", check=True)
        self.assertEqual(p.stdout, "-B /a:/b --rootfs=x ")

    def test_missing_command(self):
        p = ds("-n", self.c.name, "run")
        self.assertNotEqual(p.returncode, 0)


class StartUmask(DsTest):
    def test_strict_caller_umask_does_not_reach_the_container(self):
        # A container's setup and init get 022 whoever starts it: a 077 shell
        # would leave / and /run/droidspaces 0700 for non-root users.
        c = self.container("umask", start=False)  # not the cached base rootfs
        old = os.umask(0o077)
        try:
            c.start()
        finally:
            os.umask(old)
        self.assertEqual(c.sh("grep Umask /proc/1/status | cut -f2"), "0022")
        self.assertEqual(c.sh("stat -c %a /run/droidspaces"), "755")


class Enter(DsTest):
    @classmethod
    def setUpClass(cls):
        cls.c = cls.shared_container("enter")

    def test_enter_as_root(self):
        text, code = self.c.enter("echo MARK-$(id -un)-$(hostname)")
        self.assertIn(f"MARK-root-{self.c.name}", text)
        self.assertEqual(code, 0)

    def test_enter_as_user(self):
        text, code = self.c.enter("echo MARK-$(id -un)", user="tester")
        self.assertIn("MARK-tester", text)
        self.assertEqual(code, 0)

    def test_enter_needs_a_tty(self):
        p = ds("-n", self.c.name, "enter")
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Interactive terminal is required", p.stdout + p.stderr)


class Foreground(DsTest):
    def test_foreground_start_stays_attached_until_stop(self):
        c = Container("fg").prepare()
        self.addCleanup(c.cleanup)
        pid, fd = pty.fork()
        if pid == 0:  # pragma: no cover
            os.execvp(DS, [DS, "-n", c.name, "-r", str(c.rootfs), "--reset", "-f", "start"])
        out, stopped, deadline = b"", False, time.time() + 60
        try:
            while time.time() < deadline:
                r, _, _ = select.select([fd], [], [], 0.5)
                if r:
                    try:
                        chunk = os.read(fd, 65536)
                    except OSError:
                        break
                    if not chunk:
                        break
                    out += chunk
                if not stopped and c.pid() is not None:
                    time.sleep(2)
                    c.stop()
                    stopped = True
            else:
                os.kill(pid, 9)
        finally:
            os.close(fd)
        _, status = os.waitpid(pid, 0)
        self.assertTrue(stopped, "container never came up")
        self.assertEqual(os.waitstatus_to_exitcode(status), 0, strip_ansi(out.decode(errors="replace"))[-500:])
        self.assertIsNone(c.pid())


class Init(DsTest):
    def test_custom_init(self):
        c = self.container("init", start=False)
        (c.rootfs / "test-init").write_text("#!/bin/sh\necho INIT-MARK > /run/init-mark\nexec /sbin/init\n")
        (c.rootfs / "test-init").chmod(0o755)
        c.start("--init=/test-init")
        self.assertEqual(c.sh("cat /run/init-mark"), "INIT-MARK")
        self.assertEqual(c.saved_config()["custom_init"], "/test-init")

    def test_init_that_exits_takes_the_container_down(self):
        c = self.container("init-dies", "--init=/bin/false", expect=None)
        self.assertTrue(c.wait_dead(20), "monitor did not reap a dead init")

    def test_missing_init_is_refused(self):
        c = self.container("init-missing", start=False)
        p = c.start("--init=/no/such/init", expect=None)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Init binary not found", p.stdout + p.stderr)
        self.assertIsNone(c.pid())


class Environment(DsTest):
    def test_env_file(self):
        c = self.container("env", start=False)
        env = c.work / "env"
        env.write_text('# comment\nFOO=bar\nQUOTED="q v"\nexport EXP=1\nSINGLE=\'s\'\n')
        c.start("-E", str(env))
        init_env = c.sh("tr '\\0' '\\n' < /proc/1/environ")
        for line in ("FOO=bar", "QUOTED=q v", "EXP=1", "SINGLE=s"):
            self.assertIn(line, init_env.splitlines())
        # login shells get the same variables through /etc/profile.d
        self.assertEqual(c.sh(". /etc/profile.d/droidspaces_env.sh; echo $FOO-$QUOTED"), "bar-q v")
        self.assertEqual(c.sh("echo $FOO-$QUOTED", env_file=env), "bar-q v")
        self.assertEqual(c.saved_config()["env_file"], str(env))


class Binds(DsTest):
    def test_bind_mounts(self):
        c = self.container("bind", start=False)
        (c.work / "share").mkdir()
        (c.work / "share/f").write_text("hello\n")
        c.start("-B", f"{c.work}/share:/mnt/ro:ro", "-B", f"{c.work}/share:/mnt/rw,{c.work}/share:/deep/er/path")
        self.assertEqual(c.sh("cat /mnt/ro/f; cat /mnt/rw/f; cat /deep/er/path/f"), "hello\nhello\nhello")
        p = c.run("touch", "/mnt/ro/x", check=False)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("Read-only file system", p.stderr)
        c.run("touch", "/mnt/rw/x")
        self.assertTrue((c.work / "share/x").exists())
        self.assertEqual(c.info()["bind_mount_count"], 3)
        self.assertIn(f"{c.work}/share:/mnt/ro:ro", c.saved_config()["bind_mounts"])

    def test_missing_source_warns_but_starts(self):
        c = self.container("bind-missing", start=False)
        p = c.start("-B", f"{c.work}/nope:/mnt/nope")
        self.assertIn("Failed to bind mount", p.stdout + p.stderr)
        self.assertIsNotNone(c.pid())


class Volatile(DsTest):
    def test_volatile_discards_changes_and_is_saved(self):
        c = self.container("volatile", "-V")
        c.run("touch", "/mark-one")
        self.assertEqual(c.info()["volatile_mode"], 1)
        c.stop()
        self.assertFalse((c.rootfs / "mark-one").exists())
        c.start(rootfs=False, reset=False)
        self.assertEqual(c.saved_config()["volatile_mode"], "1")
        c.run("touch", "/mark-two")
        c.stop()
        self.assertFalse((c.rootfs / "mark-two").exists())

    def test_changes_persist_without_volatile(self):
        c = self.container("persist")
        c.run("touch", "/mark")
        c.stop()
        self.assertTrue((c.rootfs / "mark").exists())


class Image(DsTest):
    def test_rootfs_image(self):
        if shutil.which("mkfs.ext4") is None:
            self.skipTest("mkfs.ext4 not installed")
        c = self.container("img", start=False)
        img = c.work / "root.img"
        subprocess.run(["truncate", "-s", "96M", str(img)], check=True)
        subprocess.run(["mkfs.ext4", "-q", "-F", "-d", str(c.rootfs), str(img)], check=True)
        c.start("-i", str(img), rootfs=False)
        mounts = Path("/proc/self/mountinfo").read_text()
        self.assertIn(f" /mnt/Droidspaces/{c.name} ", mounts)
        self.assertTrue(c.sh("cat /etc/passwd"))
        self.assertEqual(c.saved_config()["rootfs_path"], str(img))
        c.stop()
        self.assertNotIn(f" /mnt/Droidspaces/{c.name} ", Path("/proc/self/mountinfo").read_text())


class Records(DsTest):
    def test_scan_keeps_a_running_container(self):
        c = self.container("scan")
        p = ds("scan", check=True)
        self.assertIn("Scanning system for untracked Droidspaces containers", p.stdout)
        self.assertIsNotNone(c.pid())
        self.assertIn(c.name, [e["name"] for e in ds_json("show")["running"]])

    def test_logs_are_written_and_readable(self):
        c = self.container("logs")
        c.stop()
        self.assertIn(c.name, c.log())
        self.assertIn("stop", c.log().lower())
        self.assertTrue((WORKSPACE / "Logs" / c.name / "console").exists())

    def test_info_json_keys(self):
        c = self.container("infokeys")
        d = c.info()
        for key in ("host_platform", "host_arch", "name", "pid", "os", "hostname", "ip", "ip6", "uptime_sec", "uptime",
                    "ram_used_kb", "cpu_permill", "ram_limit_kb", "cpu_limit_permill", "swap_allowed", "ram_total_kb",
                    "networking_mode", "nat_ip", "disable_ipv6", "hw_access", "volatile_mode", "force_cgroup_v1",
                    "memory_limit", "cpu_quota", "cpu_period", "pids_limit", "sandboxing_allowed", "vts_allowed",
                    "foreground_mode", "dns_servers", "port_forwards", "bind_mount_count", "env_var_count"):
            self.assertIn(key, d)
        self.assertEqual((d["host_platform"], d["pid"]), ("Linux", c.pid()))
        self.assertIn(c.name, ds("-n", c.name, "info", check=True).stdout)
