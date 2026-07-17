"""Shared QNX QEMU runner.

Unifies the QEMU/serial/login logic that previously lived in:
- ``tools/qnx_run.sh`` (embedded Python heredoc)
- ``tools/qnx_run_v8_unittests.py`` (``QNXSerial`` class)

Every test module under ``qnx_tests.modules`` and the top-level
``qnx_tests.cli`` should depend on this module rather than reimplementing
serial I/O.  Adding a new test module therefore does not require
duplicating boot/login code.

The wire protocol is identical to the historical implementation:
- ``__PI_QNX_READY__`` is echoed after a clean shell prompt is seen
- each command is wrapped as ``sh -c '<cmd>'; echo __PI_QNX_EXIT__:$?``
- exit code is parsed from the ``__PI_QNX_EXIT__:<n>`` marker line
"""

from __future__ import annotations

import ipaddress
import os
import re
import socket
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Iterable, Optional, Sequence, Tuple


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

READY_MARKER = "__PI_QNX_READY__"
EXIT_MARKER = "__PI_QNX_EXIT__"
EXIT_RE = re.compile(rb"__PI_QNX_EXIT__:(\d+)")


def q(s: str) -> str:
    """Single-quote a string for embedding inside ``sh -c '...'``."""
    return "'" + s.replace("'", "'\\''") + "'"


def validate_dns_server(value: str) -> str:
    """Validate a guest nameserver address and return it unchanged."""
    if not value:
        return ""
    try:
        address = ipaddress.ip_address(value)
    except ValueError as exc:
        raise ValueError(
            f"QNX DNS server must be a valid IP literal (got: {value})"
        ) from exc
    if (address.is_loopback or address.is_unspecified or
            address.is_multicast or address.is_link_local):
        raise ValueError(
            f"QNX DNS server must be a reachable non-loopback address (got: {value})"
        )
    return value


def discover_dns_server() -> str:
    """Return the first usable host resolver, or an empty string.

    The host's /etc/resolv.conf may point at the local systemd-resolved stub
    (127.0.0.53), which is not reachable from the QNX guest. Prefer the
    link-specific addresses reported by resolvectl and skip unusable values.
    """
    candidates = []
    try:
        result = subprocess.run(
            ["resolvectl", "dns"],
            capture_output=True,
            text=True,
            check=False,
        )
        candidates.extend(result.stdout.split())
    except OSError:
        pass
    try:
        with open("/etc/resolv.conf", encoding="utf-8") as fp:
            for line in fp:
                fields = line.split()
                if fields and fields[0] == "nameserver":
                    candidates.extend(fields[1:2])
    except OSError:
        pass
    for candidate in candidates:
        if not re.search(r"[.:]", candidate):
            continue
        try:
            return validate_dns_server(candidate)
        except ValueError:
            continue
    return ""


def looks_like_shell_prompt(buf: bytes) -> bool:
    """Heuristic: did the guest just print a shell prompt we can act on?"""
    tail = buf[-800:]
    return bool(
        re.search(rb"(?m)(^|[\r\n])[^\r\n]{0,100}#\s*$", tail)
        or re.search(rb"(?m)(^|[\r\n])[^\r\n]{0,100}\$.*#?\s*$", tail)
    )


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

@dataclass
class QNXConfig:
    """Resolved runner configuration.  Mirrors the env vars read by
    ``qnx_run.sh`` so existing CHROMIUM_SRC / BUILD_DIR / QNX_DIR
    overrides continue to work.
    """

    chromium_src: str
    build_dir: str
    qnx_dir: str
    qemu_dir: str
    serial_port: int = 10024
    boot_timeout: int = 120
    cmd_timeout: int = 1800
    keep_qemu: bool = False
    dns_server: str = ""
    kill_existing: bool = False
    tap_required: bool = True

    @classmethod
    def from_env(cls, script_dir: str) -> "QNXConfig":
        chromium_src = os.environ.get(
            "CHROMIUM_SRC",
            os.path.normpath(os.path.join(script_dir, "..", "..")),
        )
        build_dir = os.environ.get(
            "BUILD_DIR",
            os.path.join(chromium_src, "out", "qnx_release"),
        )
        qnx_dir = os.environ.get("QNX_DIR", os.path.expanduser("~/qnx800"))
        qemu_dir = os.environ.get(
            "QEMU_DIR", os.path.join(qnx_dir, "images", "qemu", "qemu")
        )
        dns_server = os.environ.get("QNX_DNS_SERVER", "")
        if not dns_server:
            dns_server = discover_dns_server()
        return cls(
            chromium_src=chromium_src,
            build_dir=build_dir,
            qnx_dir=qnx_dir,
            qemu_dir=qemu_dir,
            serial_port=int(os.environ.get("SERIAL_PORT", "10024")),
            boot_timeout=int(os.environ.get("BOOT_TIMEOUT", "120")),
            cmd_timeout=int(os.environ.get("CMD_TIMEOUT", "1800")),
            dns_server=validate_dns_server(dns_server),
        )

    def guest_build_dir(self) -> str:
        rel = os.path.relpath(self.build_dir, self.chromium_src)
        if rel == ".":
            return "/mnt/nfs"
        if rel.startswith(".."):
            raise ValueError(
                f"BUILD_DIR must live under CHROMIUM_SRC\n"
                f"  CHROMIUM_SRC={self.chromium_src}\n"
                f"  BUILD_DIR={self.build_dir}"
            )
        return f"/mnt/nfs/{rel}"


# ---------------------------------------------------------------------------
# Serial / login
# ---------------------------------------------------------------------------

class QNXSerial:
    """Talk to a QNX QEMU guest over a TCP serial port.

    Same protocol as ``qnx_run.sh`` and the previous
    ``qnx_run_v8_unittests.py`` implementation, with one small
    refinement: a single QEMU session is reused across as many
    ``run_command`` calls as the caller wants, which is what makes
    per-test invocation practical.
    """

    def __init__(self, port: int, boot_timeout: int, log_path: str):
        self.port = port
        self.boot_timeout = boot_timeout
        self.sock: Optional[socket.socket] = None
        self.serial_fp = open(log_path, "ab", buffering=0)
        self.boot_buf = b""

    # ----- connection / login ---------------------------------------------

    def connect(self) -> None:
        deadline = time.time() + self.boot_timeout
        while time.time() < deadline:
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=2.0)
                s.setblocking(False)
                self.sock = s
                return
            except OSError:
                time.sleep(1)
        raise TimeoutError(
            f"timed out connecting to serial port {self.port}"
        )

    def _recv(self) -> bytes:
        try:
            return self.sock.recv(4096)
        except (BlockingIOError, socket.timeout):
            return b""

    def _send(self, line: str) -> None:
        self.sock.sendall((line + "\n").encode())

    def _wait_prompt(self, timeout: float = 30) -> bool:
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self._recv()
            if data:
                buf += data
                buf = buf[-8192:]
                self.serial_fp.write(data)
                if READY_MARKER.encode() in buf or looks_like_shell_prompt(buf):
                    return True
            time.sleep(0.05)
        return False

    def boot_and_login(self) -> None:
        print("=== Connected to QNX serial; waiting for login/shell ===")
        self.boot_buf = b""

        login_attempts = 0
        login_sent = False
        password_sent = False
        last_wakeup = 0.0
        ready = False
        deadline = time.time() + self.boot_timeout

        self._send("")

        while time.time() < deadline and not ready:
            data = self._recv()
            if data:
                self.boot_buf += data
                self.boot_buf = self.boot_buf[-8192:]
                text_low = self.boot_buf.lower()

                if READY_MARKER.encode() in self.boot_buf:
                    ready = True
                    break

                if b"login incorrect" in text_low:
                    login_sent = False
                    password_sent = False
                    self.boot_buf = b""
                    time.sleep(1)
                    continue

                if looks_like_shell_prompt(self.boot_buf):
                    self._send(f"echo {READY_MARKER}")
                    self.boot_buf = b""
                    continue

                if b"password:" in text_low and not password_sent:
                    self._send("root")
                    password_sent = True
                    login_sent = True
                    self.boot_buf = b""
                    continue

                if b"login:" in text_low and not login_sent:
                    if login_attempts >= 3:
                        raise TimeoutError(
                            "QNX login rejected root credentials 3 times"
                        )
                    self._send("root")
                    login_attempts += 1
                    login_sent = True
                    self.boot_buf = b""
                    continue

            now = time.time()
            if now - last_wakeup > 10 and not login_sent:
                self._send("")
                last_wakeup = now
            time.sleep(0.05)

        if not ready:
            raise TimeoutError(
                "QNX login/shell did not become ready before timeout"
            )

        print("\n=== QNX shell ready ===")

    def setup_env(
        self,
        guest_build_dir: str,
        main_binary: str,
        extra_env: Sequence[str] = (),
        dns_server: str = "",
    ) -> None:
        """Mount NFS and export the usual guest environment."""
        setup_lines = [
            "umount /mnt/nfs 2>/dev/null",
            "umount /mnt 2>/dev/null",
            "mkdir -p /mnt",
            "ifconfig vtnet0 10.0.2.2 netmask 255.255.255.0 up",
            "route add default 10.0.2.1",
            "mkdir -p /mnt/nfs",
            "fs-nfs3 10.0.2.1:/export/chromium-src /mnt/nfs",
            f"cd {guest_build_dir}",
        ]
        dns_server = validate_dns_server(dns_server)
        if dns_server:
            setup_lines.insert(
                5,
                f"printf 'nameserver %s\\n' {q(dns_server)} > /etc/resolv.conf",
            )
        env_lines = [
            f"export LD_LIBRARY_PATH={guest_build_dir}",
            "unset CHROME_EXE_PATH",
            "export CR_SOURCE_ROOT=/mnt/nfs",
        ]
        for var in extra_env:
            env_lines.append(f"export {var}")

        for line in setup_lines:
            self._send(line)
            time.sleep(0.05)
            if not self._wait_prompt(timeout=30):
                print(f"WARNING: no prompt after: {line}")
        for line in env_lines:
            self._send(line)
            time.sleep(0.05)
            if not self._wait_prompt(timeout=15):
                print(f"WARNING: no prompt after: {line}")

    # ----- command execution ----------------------------------------------

    def run_command(
        self,
        cmd: str,
        timeout: float,
        *,
        stream_output: bool = True,
    ) -> Tuple[int, bytes]:
        """Run a shell command on the guest; return ``(exit_code, raw_output)``.

        ``-1`` indicates that the per-command timeout elapsed.  Callers that
        need a machine-readable stdout stream can disable terminal streaming;
        the serial transcript is still retained in ``log_path``.
        """
        self._send(f"sh -c {q(cmd)}; echo {EXIT_MARKER}:$?")
        raw = b""
        deadline = time.time() + timeout
        marker = f"{EXIT_MARKER}:".encode()
        while time.time() < deadline:
            try:
                data = self.sock.recv(65536)
            except (BlockingIOError, socket.timeout):
                data = b""
            if data:
                raw += data
                self.serial_fp.write(data)
                if stream_output:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                idx = raw.rfind(marker)
                if idx >= 0:
                    rest = raw[idx + len(marker):]
                    m = re.match(rb"(\d+)", rest)
                    if m:
                        return int(m.group(1)), raw
            time.sleep(0.05)
        return -1, raw

    def close(self) -> None:
        if self.serial_fp:
            self.serial_fp.close()
        if self.sock:
            self.sock.close()


# ---------------------------------------------------------------------------
# QEMU lifecycle
# ---------------------------------------------------------------------------

def _check_prereqs(cfg: QNXConfig) -> None:
    for path, label in [
        (os.path.join(cfg.qemu_dir, "output", "ifs.bin"), "ifs.bin"),
        ("/export/chromium-src", "NFS export dir"),
        (cfg.build_dir, "build dir"),
    ]:
        if not os.path.exists(path):
            print(f"ERROR: {label} not found at {path}", file=sys.stderr)
            sys.exit(1)

    for cmd in ("qemu-system-x86_64", "python3"):
        if subprocess.run(["which", cmd], capture_output=True).returncode != 0:
            print(f"ERROR: {cmd} not found", file=sys.stderr)
            sys.exit(1)

    if cfg.tap_required:
        # /proc/net/dev gives us a portable way to check
        try:
            with open("/proc/net/dev") as fp:
                if "tap0" not in fp.read():
                    raise OSError()
        except OSError:
            print("ERROR: tap0 not found. Run: sudo cef/tools/qnx_setup_env.sh",
                  file=sys.stderr)
            sys.exit(1)


def _port_in_use(port: int) -> bool:
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=1)
        s.close()
        return True
    except (ConnectionRefusedError, TimeoutError, OSError):
        return False


def _disk_args(qemu_dir: str) -> list:
    raw = os.path.join(qemu_dir, "output", "disk-qemu")
    vmdk = os.path.join(qemu_dir, "output", "disk-qemu.vmdk")
    if os.path.exists(raw):
        return ["-drive", f"file={raw},format=raw,if=ide,id=drv0"]
    if os.path.exists(vmdk):
        return ["-drive", f"file={vmdk},if=ide,id=drv0"]
    print("ERROR: QEMU disk image not found", file=sys.stderr)
    sys.exit(1)


def boot_qemu(cfg: QNXConfig) -> int:
    """Start QEMU.  Returns PID."""
    if cfg.kill_existing:
        subprocess.run(["pkill", "-9", "-f", "qemu-system-x86_64"],
                       capture_output=True)
        time.sleep(1)
    elif subprocess.run(["pgrep", "-f", "qemu-system-x86_64"],
                        capture_output=True).returncode == 0:
        print("ERROR: qemu-system-x86_64 is already running.\n"
              "       Stop it first, or use --kill-existing.",
              file=sys.stderr)
        sys.exit(1)

    if _port_in_use(cfg.serial_port):
        print(f"ERROR: serial port {cfg.serial_port} is already in use.",
              file=sys.stderr)
        sys.exit(1)

    _check_prereqs(cfg)

    args = [
        "qemu-system-x86_64",
        "--enable-kvm",
        *_disk_args(cfg.qemu_dir),
        "-netdev", "tap,id=net0,ifname=tap0,script=no,downscript=no",
        "-device", "virtio-net-pci,netdev=net0",
        "-kernel", os.path.join(cfg.qemu_dir, "output", "ifs.bin"),
        "-nographic",
        "-monitor", "none",
        "-serial", f"tcp:127.0.0.1:{cfg.serial_port},server,nowait",
        "--cpu", "host,host-phys-bits-limit=40",
        "-smp", "4", "-m", "4G",
    ]

    boot_log = os.path.join(cfg.build_dir, "qnx_boot.log")
    start_new_session = cfg.keep_qemu
    proc = subprocess.Popen(
        args,
        stdin=subprocess.DEVNULL,
        stdout=open(boot_log, "w"),
        stderr=subprocess.STDOUT,
        start_new_session=start_new_session,
    )
    print(f"QEMU PID: {proc.pid}")
    return proc.pid


def kill_qemu(pid: int) -> None:
    subprocess.run(["kill", str(pid)], capture_output=True)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
