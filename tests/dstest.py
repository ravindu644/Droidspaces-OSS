"""Shared helpers for the droidspaces black-box tests.

The tests drive the installed `droidspaces` binary through the running daemon,
as an unprivileged member of the `droidspaces` group, exactly the way a user on
a Linux box does. Nothing here needs sudo. Run them with `make test` from the
repository root, or `python3 -m unittest discover -s tests -v`.

Every test container is named `dstest-<tag>` and lives in its own directory
under ~/.cache/droidspaces-tests/work/, with a private copy of the base rootfs.
The directory matters: `start` saves the config next to the rootfs, so two
containers that share a parent directory would read each other's settings.

Environment knobs, all optional:
  DS                        the binary to drive (default: droidspaces on PATH)
  DS_TEST_CACHE             cache directory (default ~/.cache/droidspaces-tests)
  DS_TEST_ROOTFS            an extracted rootfs directory or a rootfs tarball to
                            use as the base instead of downloading Alpine
  DS_TEST_MACVLAN_PARENT    a wired host NIC for the positive macvlan test

Never add `-P` (`--selinux-permissive`) to any test: on Linux it really sets the
host's SELinux to permissive and the setting is saved into the container config.
"""

import hashlib
import json
import os
import platform
import pty
import re
import select
import shutil
import subprocess
import time
import unittest
import urllib.request
from pathlib import Path

DS = os.environ.get("DS", "droidspaces")
CACHE = Path(os.environ.get("DS_TEST_CACHE", "~/.cache/droidspaces-tests")).expanduser()
WORKSPACE = Path("/var/lib/Droidspaces")
REPO = Path(__file__).resolve().parent.parent
TIMEOUT = 90
ALPINE_ARCH = {"x86_64": "x86_64", "aarch64": "aarch64", "armv7l": "armhf", "armv8l": "armhf", "i686": "x86"}

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def strip_ansi(text):
    return ANSI.sub("", text).replace("\r", "")


def ds(*args, stdin=None, check=False, timeout=TIMEOUT):
    """Run the CLI once. stdout and stderr come back stripped of ANSI and CR."""
    p = subprocess.run([DS, *args], input=stdin, capture_output=True, text=True, timeout=timeout)
    p.stdout, p.stderr = strip_ansi(p.stdout), strip_ansi(p.stderr)
    if check and p.returncode != 0:
        raise AssertionError(f"droidspaces {' '.join(args)} exited {p.returncode}\n{p.stdout}{p.stderr}")
    return p


def ds_json(*args):
    """`--json` output. Only the first line is JSON: `info` appends a human block when limits are set."""
    return json.loads(ds("--json", *args, check=True).stdout.splitlines()[0])


def tree_version():
    header = (REPO / "src/include/droidspaces.h").read_text()
    return re.search(r'#define\s+DS_VERSION\s+"([^"]+)"', header).group(1)


def require_daemon():
    """Skip a whole module when the daemon is not answering or we are not allowed in."""
    if shutil.which(DS) is None and not Path(DS).exists():
        raise unittest.SkipTest(f"{DS} not found")
    if ds("mode").stdout.strip() != "daemon":
        raise unittest.SkipTest("no droidspaces daemon answering; start it and add yourself to the droidspaces group")
    p = ds("show")
    if p.returncode != 0:
        raise unittest.SkipTest(f"daemon refused us: {p.stderr.strip()}")


_internet = None


def has_internet():
    global _internet
    if _internet is None:
        _internet = False
        for url in ("http://connectivitycheck.gstatic.com/generate_204", "http://1.1.1.1/"):
            try:
                urllib.request.urlopen(url, timeout=6).close()
                _internet = True
                break
            except Exception:
                pass
    return _internet


def running_names():
    return [c["name"] for c in ds_json("show")["running"]]


def foreign_running():
    """Containers that are not ours. Tests that rely on 'exactly one running' skip when any exist."""
    return [n for n in running_names() if not n.startswith("dstest-")]


def cap_mask(dropped=()):
    """CapBnd value for a kernel with cap_last_cap caps, minus the given cap numbers."""
    last = int(Path("/proc/sys/kernel/cap_last_cap").read_text())
    mask = (1 << (last + 1)) - 1
    for cap in dropped:
        mask &= ~(1 << cap)
    return f"{mask:016x}"


# Caps droidspaces drops from the bounding set (security/caps.c). SYS_PTRACE stays with --allow-sandboxing.
CAPS_DROPPED_ALWAYS = (16,)
CAPS_DROPPED_NO_HW = (2, 16, 17, 19, 20, 32, 33, 35, 36, 37)


def _download(url, dest):
    with urllib.request.urlopen(url, timeout=60) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)


def base_rootfs():
    """The base rootfs, prepared once and cached. Alpine minirootfs unless DS_TEST_ROOTFS says otherwise."""
    base = CACHE / "base" / "rootfs"
    if (base / "bin/busybox").exists() or (base / "etc/passwd").exists():
        return base
    shutil.rmtree(base, ignore_errors=True)
    base.mkdir(parents=True)
    src = os.environ.get("DS_TEST_ROOTFS")
    if src and Path(src).is_dir():
        subprocess.run(["cp", "-a", "--reflink=auto", f"{src}/.", str(base)], check=True)
    else:
        tarball = Path(src) if src else _alpine_tarball()
        subprocess.run(["tar", "-xf", str(tarball), "-C", str(base)], check=True)
    # A real login account for the -u, enter and /dev tests. nobody has no shell.
    with open(base / "etc/passwd", "a") as f:
        f.write("tester:x:1000:1000::/home/tester:/bin/sh\n")
    with open(base / "etc/group", "a") as f:
        f.write("tester:x:1000:\n")
    (base / "home/tester").mkdir(parents=True, exist_ok=True)
    return base


def _alpine_tarball():
    arch = ALPINE_ARCH.get(platform.machine())
    if arch is None:
        raise unittest.SkipTest(f"no Alpine minirootfs for {platform.machine()}; set DS_TEST_ROOTFS")
    dl = CACHE / "dl"
    dl.mkdir(parents=True, exist_ok=True)
    base_url = f"https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/{arch}/"
    index = dl / f"latest-releases-{arch}.yaml"
    if not index.exists():
        _download(base_url + "latest-releases.yaml", index)
    text = index.read_text()
    m = re.search(r"file: (alpine-minirootfs-\S+\.tar\.gz).*?sha256: ([0-9a-f]{64})", text, re.S)
    if not m:
        raise unittest.SkipTest("could not find the minirootfs entry in latest-releases.yaml; set DS_TEST_ROOTFS")
    name, sha = m.groups()
    tarball = dl / name
    if not tarball.exists() or hashlib.sha256(tarball.read_bytes()).hexdigest() != sha:
        _download(base_url + name, tarball)
        if hashlib.sha256(tarball.read_bytes()).hexdigest() != sha:
            raise AssertionError(f"sha256 mismatch for {name}")
    return tarball


class Container:
    """One test container with its own work directory and rootfs copy."""

    def __init__(self, tag):
        self.name = f"dstest-{tag}"
        self.work = CACHE / "work" / self.name
        self.rootfs = self.work / "rootfs"

    def prepare(self):
        self._wipe()
        self.work.mkdir(parents=True)
        subprocess.run(["cp", "-a", "--reflink=auto", str(base_rootfs()), str(self.rootfs)], check=True)
        return self

    def start(self, *flags, rootfs=True, reset=True, expect=0):
        """`start` with the rootfs and `--reset` by default, so saved config from an earlier run cannot leak in."""
        args = ["-n", self.name]
        if rootfs:
            args += ["-r", str(self.rootfs)]
        if reset:
            args.append("--reset")
        p = ds(*args, *flags, "start")
        if expect is not None and p.returncode != expect:
            raise AssertionError(f"start {' '.join(flags)} exited {p.returncode}, wanted {expect}\n{p.stdout}{p.stderr}")
        return p

    def stop(self, expect=None):
        p = ds("-n", self.name, "stop")
        if expect is not None and p.returncode != expect:
            raise AssertionError(f"stop exited {p.returncode}, wanted {expect}\n{p.stdout}{p.stderr}")
        return p

    def restart(self, *flags):
        return ds("-n", self.name, *flags, "restart", check=True)

    def pid(self):
        out = ds("-n", self.name, "pid").stdout.strip()
        return None if out == "NONE" else int(out)

    def wait_dead(self, seconds=15):
        end = time.time() + seconds
        while time.time() < end:
            if self.pid() is None:
                return True
            time.sleep(0.5)
        return False

    def info(self):
        return ds_json("-n", self.name, "info")

    def run(self, *cmd, user=None, stdin=None, env_file=None, check=True):
        args = ["-n", self.name]
        if user:
            args += ["-u", user]
        if stdin is not None:
            args.append("--stdin")
        if env_file:
            args += ["-E", str(env_file)]
        p = ds(*args, "run", "--", *cmd, stdin=stdin)
        if check and p.returncode != 0:
            raise AssertionError(f"run {' '.join(cmd)} exited {p.returncode}\n{p.stdout}{p.stderr}")
        return p

    def sh(self, script, **kw):
        """Run a shell snippet inside and return its stdout without the trailing newline."""
        return self.run("sh", "-c", script, **kw).stdout.rstrip("\n")

    def dhcp(self):
        """Alpine's init does not run a DHCP client, so NAT tests ask for the lease themselves."""
        self.sh("udhcpc -i eth0 -n -q -t 5 >/dev/null 2>&1")
        return self.ip4()

    def ip4(self, iface="eth0"):
        m = re.search(r"inet (\d+\.\d+\.\d+\.\d+)", self.sh(f"ip -4 -o addr show {iface} 2>/dev/null"))
        return m.group(1) if m else None

    def links(self):
        return [l.split(":")[1].strip().split("@")[0] for l in self.sh("ip -o link").splitlines()]

    def mountinfo(self):
        return self.sh("cat /proc/self/mountinfo")

    def fstype(self, path):
        """Filesystem type mounted at `path` inside, from the field after the '-' separator in mountinfo."""
        return self.sh(f"awk '$5==\"{path}\"{{for(i=7;i<=NF;i++) if($i==\"-\"){{print $(i+1); exit}}}}' /proc/self/mountinfo")

    @property
    def dirname(self):
        """Folder name the daemon uses for this container: the name with spaces turned into dashes."""
        return self.name.replace(" ", "-")

    def cgroup(self, file, missing=None):
        """A file in the container's cgroup2 dir. `missing` is returned instead of raising when the
        controller behind it is not enabled there, which droidspaces only does once a container
        sets that limit."""
        path = Path("/sys/fs/cgroup/droidspaces") / self.dirname / file
        if missing is not None and not path.exists():
            return missing
        return path.read_text().strip()

    def log(self):
        return (WORKSPACE / "Logs" / self.dirname / "log").read_text()

    def saved_config(self):
        """The workspace copy of the config, which every start rewrites. A `-r` start also writes one next to the rootfs."""
        return dict(
            line.split("=", 1)
            for line in (WORKSPACE / "Containers" / self.dirname / "container.config").read_text().splitlines()
            if "=" in line and not line.startswith("#")
        )

    def enter(self, script, user=None, timeout=30):
        """Drive `enter` on a pty: wait for a shell prompt, send the script, return everything printed."""
        return pty_session(["-n", self.name, "enter"] + ([user] if user else []), script, timeout)

    def cleanup(self):
        if self.pid() is not None:
            # Every `run` logs into <rootfs>/var/lib/Droidspaces/Logs/<name>/ as root. That tree is the one
            # thing we cannot delete from outside, so ask the container to remove it before it goes down.
            self.run("rm", "-rf", "/var/lib/Droidspaces/Logs", check=False)
            self.stop()
        self._wipe()

    def _wipe(self):
        """Delete the work directory. Anything root left behind that we cannot remove is moved aside."""
        shutil.rmtree(self.work, ignore_errors=True)
        if self.work.exists():
            stale = CACHE / "work" / ".stale"
            stale.mkdir(parents=True, exist_ok=True)
            self.work.rename(stale / f"{self.dirname}-{int(time.time())}")


def pty_session(args, script, timeout=30, prompt=re.compile(r"[#$] $")):
    """Run the CLI on a pseudo-terminal, type `script` once a prompt shows, and collect the output."""
    pid, fd = pty.fork()
    if pid == 0:  # pragma: no cover - child
        os.execvp(DS, [DS, *args])
    out, sent = b"", False
    end = time.time() + timeout
    try:
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.5)
            if r:
                try:
                    chunk = os.read(fd, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                out += chunk
            if not sent and prompt.search(strip_ansi(out.decode(errors="replace"))):
                os.write(fd, (script + "\nexit\n").encode())
                sent = True
        else:
            os.kill(pid, 9)
    finally:
        os.close(fd)
    _, status = os.waitpid(pid, 0)
    text = strip_ansi(out.decode(errors="replace"))
    if not sent:
        raise AssertionError(f"no shell prompt within {timeout}s:\n{text[-500:]}")
    return text, os.waitstatus_to_exitcode(status)


class DsTest(unittest.TestCase):
    """Base class: `self.container(tag, *flags)` gives a started container that is stopped and wiped after the test."""

    maxDiff = None

    def container(self, tag, *flags, start=True, **kw):
        c = Container(tag).prepare()
        self.addCleanup(c.cleanup)
        if start:
            c.start(*flags, **kw)
        return c

    @classmethod
    def shared_container(cls, tag, *flags, **kw):
        """A container that lives for the whole class, for read-only assertions."""
        c = Container(tag).prepare()
        cls.addClassCleanup(c.cleanup)
        c.start(*flags, **kw)
        return c
