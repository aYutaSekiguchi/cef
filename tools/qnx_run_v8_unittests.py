#!/usr/bin/env python3
"""
Run v8_unittests on QNX inside QEMU, one test at a time.

V8's test fixtures (WithDefaultPlatformMixin) do not support running
multiple test cases in the same process.  The upstream V8 test runner
works around this by launching a separate process per test via
--gtest_filter.  This script does the same over QEMU serial.

Usage:
  sudo ./cef/tools/qnx_setup_env.sh          # first, if not already done
  ./cef/tools/qnx_run_v8_unittests.py        # boot QEMU + run all tests
  ./cef/tools/qnx_run_v8_unittests.py --filter 'InspectorTest.*'
  ./cef/tools/qnx_run_v8_unittests.py --skip-death-tests
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

# ---------------------------------------------------------------------------
# Paths and defaults (mirrors qnx_run.sh)
# ---------------------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CEF_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
CHROMIUM_SRC = os.environ.get("CHROMIUM_SRC",
                              os.path.normpath(os.path.join(CEF_DIR, "..")))
BUILD_DIR = os.environ.get("BUILD_DIR",
                           os.path.join(CHROMIUM_SRC, "out", "qnx_release"))
QNX_DIR = os.environ.get("QNX_DIR", os.path.expanduser("~/qnx800"))
QEMU_DIR = os.path.join(QNX_DIR, "images", "qemu", "qemu")

SERIAL_PORT = int(os.environ.get("SERIAL_PORT", "10024"))
BOOT_TIMEOUT = int(os.environ.get("BOOT_TIMEOUT", "120"))
TEST_TIMEOUT = int(os.environ.get("TEST_TIMEOUT", "600"))

READY_MARKER = "__PI_QNX_READY__"
EXIT_MARKER = "__PI_V8_EXIT__"
EXIT_RE = re.compile(rb"__PI_V8_EXIT__:(\d+)")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def q(s: str) -> str:
    """Shell-quote a string for use inside sh -c."""
    return "'" + s.replace("'", "'\\''") + "'"


def build_dir_relative() -> str:
    """Relative path from CHROMIUM_SRC to BUILD_DIR."""
    rel = os.path.relpath(BUILD_DIR, CHROMIUM_SRC)
    if rel == ".":
        return "."
    if rel.startswith(".."):
        print(f"ERROR: BUILD_DIR must live under CHROMIUM_SRC\n"
              f"  CHROMIUM_SRC={CHROMIUM_SRC}\n"
              f"  BUILD_DIR={BUILD_DIR}",
              file=sys.stderr)
        sys.exit(1)
    return rel


GUEST_BUILD_DIR = f"/mnt/nfs/{build_dir_relative()}" if build_dir_relative() != "." else "/mnt/nfs/out/qnx_release"


def check_file(path: str, label: str = ""):
    if not os.path.exists(path):
        msg = label or path
        print(f"ERROR: {msg} not found at {path}", file=sys.stderr)
        sys.exit(1)


# ---------------------------------------------------------------------------
# Serial communication (adapted from qnx_run.sh)
# ---------------------------------------------------------------------------

def looks_like_shell_prompt(buf: bytes) -> bool:
    tail = buf[-800:]
    return bool(re.search(rb"(?m)(^|[\r\n])[^\r\n]{0,100}#\s*$", tail) or
                re.search(rb"(?m)(^|[\r\n])[^\r\n]{0,100}\$.*#?\s*$", tail))


class QNXSerial:
    """Talk to a QNX QEMU guest over the TCP serial port."""

    def __init__(self, port: int, boot_timeout: int, log_path: str):
        self.port = port
        self.boot_timeout = boot_timeout
        self.sock: Optional[socket.socket] = None
        self.serial_fp = open(log_path, "ab", buffering=0)
        self.boot_buf = b""

    # ------------------------------------------------------------------
    # Connection & login
    # ------------------------------------------------------------------

    def connect(self):
        deadline = time.time() + self.boot_timeout
        while time.time() < deadline:
            try:
                s = socket.create_connection(("127.0.0.1", self.port), timeout=2.0)
                s.setblocking(False)
                self.sock = s
                return
            except OSError:
                time.sleep(1)
        raise TimeoutError(f"timed out connecting to serial port {self.port}")

    def _recv(self) -> bytes:
        try:
            return self.sock.recv(4096)
        except (BlockingIOError, socket.timeout):
            return b""

    def _send(self, line: str):
        self.sock.sendall((line + "\n").encode())

    def _drain_until(self, timeout: float, patterns: list) -> Optional[int]:
        """Read serial until one of the patterns matches, or timeout."""
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self._recv()
            if data:
                buf += data
                self.serial_fp.write(data)
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                buf = buf[-16384:]
                for i, pat in enumerate(patterns):
                    if isinstance(pat, re.Pattern):
                        m = pat.search(buf)
                        if m:
                            return (int(m.group(1))
                                    if m.lastindex and m.group(1) else 0)
                    elif isinstance(pat, bytes):
                        if pat in buf:
                            return i
            time.sleep(0.05)
        return None

    def boot_and_login(self):
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
                            "QNX login rejected root credentials 3 times")
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
            raise TimeoutError("QNX login/shell did not become ready before timeout")

        print("\n=== QNX shell ready; running setup ===")

    def setup_env(self, extra_env: list = None):
        """Mount NFS and export environment variables."""
        setup_lines = [
            "umount /mnt/nfs 2>/dev/null",
            "umount /mnt 2>/dev/null",
            "mkdir -p /mnt",
            "ifconfig vtnet0 10.0.2.2 netmask 255.255.255.0 up",
            "route add default 10.0.2.1",
            "mkdir -p /mnt/nfs",
            "fs-nfs3 10.0.2.1:/export/chromium-src /mnt/nfs",
            f"cd {GUEST_BUILD_DIR}",
        ]
        env_lines = [
            f"export LD_LIBRARY_PATH={GUEST_BUILD_DIR}",
            f"export CHROME_EXE_PATH={GUEST_BUILD_DIR}/v8_unittests",
            "export CR_SOURCE_ROOT=/mnt/nfs",
        ]
        if extra_env:
            env_lines.extend(f"export {e}" for e in extra_env)

        for line in setup_lines:
            self._send(line)
            time.sleep(0.05)
            self._wait_prompt(timeout=30)

        for line in env_lines:
            self._send(line)
            time.sleep(0.05)
            self._wait_prompt(timeout=15)

    def _wait_prompt(self, timeout: float = 30) -> bool:
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self._recv()
            if data:
                buf += data
                buf = buf[-8192:]
                self.serial_fp.write(data)
                if (READY_MARKER.encode() in buf or
                        looks_like_shell_prompt(buf)):
                    return True
            time.sleep(0.05)
        return False

    # ------------------------------------------------------------------
    # Command execution
    # ------------------------------------------------------------------

    def run_command(self, cmd: str, timeout: float) -> tuple[int, bytes]:
        """Run a shell command on the guest and return (exit_code, raw_output)."""
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
                idx = raw.rfind(marker)
                if idx >= 0:
                    rest = raw[idx + len(marker):]
                    m = re.match(rb"(\d+)", rest)
                    if m:
                        return int(m.group(1)), raw
            time.sleep(0.05)
        return -1, raw  # timeout

    def close(self):
        if self.serial_fp:
            self.serial_fp.close()
        if self.sock:
            self.sock.close()


# ---------------------------------------------------------------------------
# QEMU lifecycle
# ---------------------------------------------------------------------------

def boot_qemu(kill_existing: bool = False, keep_qemu: bool = False) -> int:
    """Start QEMU.  Returns PID."""
    if kill_existing:
        subprocess.run(["pkill", "-9", "-f", "qemu-system-x86_64"],
                       capture_output=True)
        time.sleep(1)
    elif subprocess.run(["pgrep", "-f", "qemu-system-x86_64"],
                        capture_output=True).returncode == 0:
        print("ERROR: qemu-system-x86_64 is already running.\n"
              "       Stop it first, or use --kill-existing.",
              file=sys.stderr)
        sys.exit(1)

    # Check port availability
    try:
        s = socket.create_connection(("127.0.0.1", SERIAL_PORT), timeout=1)
        s.close()
        print(f"ERROR: serial port {SERIAL_PORT} is already in use.", file=sys.stderr)
        sys.exit(1)
    except (ConnectionRefusedError, TimeoutError, OSError):
        pass

    for cmd in ("qemu-system-x86_64", "python3"):
        if subprocess.run(["which", cmd], capture_output=True).returncode != 0:
            print(f"ERROR: {cmd} not found", file=sys.stderr)
            sys.exit(1)

    disk_raw = os.path.join(QEMU_DIR, "output", "disk-qemu")
    disk_vmdk = os.path.join(QEMU_DIR, "output", "disk-qemu.vmdk")
    if os.path.exists(disk_raw):
        disk_args = ["-drive", f"file={disk_raw},format=raw,if=ide,id=drv0"]
    elif os.path.exists(disk_vmdk):
        disk_args = ["-drive", f"file={disk_vmdk},if=ide,id=drv0"]
    else:
        print("ERROR: QEMU disk image not found", file=sys.stderr)
        sys.exit(1)

    check_file(os.path.join(QEMU_DIR, "output", "ifs.bin"), "ifs.bin")
    check_file("/export/chromium-src", "NFS export dir")
    check_file(BUILD_DIR, "build dir")
    check_file(os.path.join(BUILD_DIR, "v8_unittests"), "v8_unittests binary")

    args = [
        "qemu-system-x86_64",
        "--enable-kvm",
        *disk_args,
        "-netdev", "tap,id=net0,ifname=tap0,script=no,downscript=no",
        "-device", "virtio-net-pci,netdev=net0",
        "-kernel", os.path.join(QEMU_DIR, "output", "ifs.bin"),
        "-nographic",
        "-monitor", "none",
        "-serial", f"tcp:127.0.0.1:{SERIAL_PORT},server,nowait",
        "--cpu", "host,host-phys-bits-limit=40",
        "-smp", "4", "-m", "4G",
    ]

    boot_log = os.path.join(BUILD_DIR, "qnx_v8_boot.log")
    proc = subprocess.Popen(
        args,
        stdin=subprocess.DEVNULL,
        stdout=open(boot_log, "w"),
        stderr=subprocess.STDOUT,
    )
    pid = proc.pid
    print(f"QEMU PID: {pid}")
    return pid


def kill_qemu(pid: int):
    subprocess.run(["kill", str(pid)], capture_output=True)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass


# ---------------------------------------------------------------------------
# Test list parsing
# ---------------------------------------------------------------------------

def parse_test_list(output: bytes) -> list[str]:
    """Parse GTest --gtest_list_tests output into fully-qualified test names.

    Format:
        TestSuite1.
          TestName1
          TestName2
        TestSuite2.
          TestName1
          TestNameWithSlash/0  # GetParam() = ...
    """
    tests = []
    current_suite = ""
    for line in output.decode("utf-8", errors="replace").splitlines():
        # Strip ANSI escapes
        line = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", line)
        line = re.sub(r"\x1b\?[0-9;]*[a-zA-Z]", "", line)
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(".") and not stripped.startswith(" "):
            # Suite name
            current_suite = stripped.rstrip(".")
        elif current_suite and stripped and not stripped.startswith("#"):
            # Remove inline comment (e.g. "# GetParam() = ...")
            test_name = stripped.split("#")[0].strip()
            if test_name:
                tests.append(f"{current_suite}.{test_name}")
    return tests


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

@dataclass
class TestResult:
    name: str
    passed: bool
    exit_code: int
    duration_s: float
    output_snippet: str = ""


def main():
    parser = argparse.ArgumentParser(
        description="Run v8_unittests on QNX under QEMU, one test at a time.")
    parser.add_argument("--filter", default="",
                        help="GTest filter (e.g. 'InspectorTest.*')")
    parser.add_argument("--skip-death-tests", action="store_true",
                        help="Skip *DeathTest* tests")
    parser.add_argument("--timeout", type=int, default=TEST_TIMEOUT,
                        help=f"Per-test timeout in seconds (default {TEST_TIMEOUT})")
    parser.add_argument("--boot-timeout", type=int, default=BOOT_TIMEOUT,
                        help=f"QEMU boot timeout (default {BOOT_TIMEOUT})")
    parser.add_argument("--kill-existing", action="store_true",
                        help="Kill any stale QEMU process first")
    parser.add_argument("--dry-run", action="store_true",
                        help="Only list tests, don't run them")
    parser.add_argument("--max-tests", type=int, default=0,
                        help="Stop after running this many tests (for testing the script)")
    args = parser.parse_args()

    stamp = time.strftime("%Y%m%d_%H%M%S")
    serial_log = os.path.join(BUILD_DIR, f"qnx_v8_unittests_{stamp}.serial.log")
    result_log = os.path.join(BUILD_DIR, f"qnx_v8_unittests_{stamp}.results.log")

    print(f"=== V8 Unittests Runner ===")
    print(f"Build dir: {BUILD_DIR}")
    print(f"Serial log: {serial_log}")

    # 1. Boot QEMU
    qemu_pid = boot_qemu(kill_existing=args.kill_existing)

    try:
        # 2. Connect and setup
        serial = QNXSerial(SERIAL_PORT, args.boot_timeout, serial_log)
        serial.connect()
        serial.boot_and_login()
        serial.setup_env()

        # 3. List tests
        print("\n=== Listing tests ===")
        exit_code, raw = serial.run_command(
            "./v8_unittests --gtest_list_tests", timeout=120)
        if exit_code != 0:
            print(f"ERROR: test listing failed (exit {exit_code})", file=sys.stderr)
            sys.exit(1)

        all_tests = parse_test_list(raw)
        print(f"Found {len(all_tests)} tests total")

        # Apply filters (support GTest-style : separator and * wildcard)
        filtered = all_tests
        if args.filter:
            patterns = []
            for part in args.filter.split(":"):
                part = part.strip()
                if not part:
                    continue
                # Convert GTest wildcard to regex
                pattern_str = part.replace("*", ".*")
                patterns.append(re.compile(pattern_str))
            filtered = [
                t for t in filtered
                if any(p.search(t) for p in patterns)
            ]
            print(f"After --filter '{args.filter}': {len(filtered)} tests")
        if args.skip_death_tests:
            before = len(filtered)
            filtered = [t for t in filtered if "DeathTest" not in t]
            print(f"After --skip-death-tests: {len(filtered)} tests "
                  f"(removed {before - len(filtered)})")

        if args.dry_run:
            print("\n=== Tests (dry run) ===")
            for t in filtered:
                print(f"  {t}")
            print(f"\nTotal: {len(filtered)} tests")
            return

        # 4. Run each test
        print(f"\n=== Running {len(filtered)} tests ===")
        if args.max_tests > 0:
            filtered = filtered[:args.max_tests]
            print(f"(limited to {args.max_tests} tests)")

        results: list[TestResult] = []
        total = len(filtered)

        for idx, test_name in enumerate(filtered, 1):
            pct = f"[{idx}/{total}]"
            print(f"\n{pct} {test_name}")
            t0 = time.time()
            ec, raw_out = serial.run_command(
                f"./v8_unittests --gtest_filter={q(test_name)} 2>&1",
                timeout=args.timeout)
            elapsed = time.time() - t0
            passed = (ec == 0)

            # Extract a snippet of the test output for diagnostics
            text = raw_out.decode("utf-8", errors="replace")
            # Find test result line
            snippet = ""
            for line in text.splitlines():
                if ("[  PASSED  ]" in line or "[  FAILED  ]" in line
                        or "[       OK ]" in line):
                    snippet = line.strip()
                elif "trace trap" in line or "core dumped" in line:
                    snippet = line.strip()

            results.append(TestResult(
                name=test_name, passed=passed,
                exit_code=ec, duration_s=round(elapsed, 1),
                output_snippet=snippet,
            ))

            status = "PASS" if passed else f"FAIL (exit {ec})"
            print(f"  {status} ({elapsed:.1f}s)")

        # 5. Summary
        print(f"\n{'='*60}")
        print(f"RESULTS: {len(results)} tests")
        passed_count = sum(1 for r in results if r.passed)
        failed_count = total - passed_count
        print(f"  PASSED: {passed_count}")
        print(f"  FAILED: {failed_count}")

        if failed_count > 0:
            print(f"\n  FAILED TESTS:")
            for r in results:
                if not r.passed:
                    print(f"    {r.name} (exit {r.exit_code}, {r.duration_s}s)")
                    if r.output_snippet:
                        print(f"      -> {r.output_snippet}")

        # Write result log
        with open(result_log, "w") as f:
            f.write(f"v8_unittests run on QNX/QEMU at {stamp}\n")
            f.write(f"Total: {total}, Passed: {passed_count}, Failed: {failed_count}\n\n")
            for r in results:
                f.write(f"{'PASS' if r.passed else 'FAIL'} | {r.name} | {r.duration_s}s")
                if not r.passed:
                    f.write(f" | exit {r.exit_code}")
                f.write("\n")

        print(f"\nFull result log: {result_log}")
        sys.exit(0 if failed_count == 0 else 1)

    finally:
        serial.close()
        kill_qemu(qemu_pid)
        print("QEMU stopped.")


if __name__ == "__main__":
    main()
