"""Command-line entry point for the QNX QEMU test runner.

Examples::

    tools/qnx_run_test.sh --all
    tools/qnx_run_test.sh --base
    tools/qnx_run_test.sh --v8
    tools/qnx_run_test.sh --swiftshader
    tools/qnx_run_test.sh --angle
    tools/qnx_run_test.sh --list
    tools/qnx_run_test.sh --base 'ProcessTest.*'
    tools/qnx_run_test.sh --cmd './base_unittests --gtest_list_tests'

The script preserves backward compatibility with the historical
``qnx_run_test.sh`` CLI: a bare invocation with no module flag
defaults to ``--base`` (which is what the old script did).  A
positional gtest filter is treated as the filter for the default
module.  The flags ``--binary``, ``--cmd``, ``--timeout``,
``--boot-timeout``, ``--serial-port``, ``--keep-qemu``,
``--mount-only`` and ``--kill-existing`` behave exactly as they did
in ``qnx_run.sh``.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

# Ensure the parent of this file (tools/) is on sys.path so that
# ``import qnx_tests`` works whether the script is invoked directly
# (``python3 tools/qnx_tests/cli.py``) or via ``python3 -m qnx_tests.cli``.
_HERE = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.dirname(_HERE)
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from qnx_tests.common import (  # noqa: E402
    QNXConfig, QNXSerial, boot_qemu, kill_qemu,
)
from qnx_tests.modules import MODULES  # noqa: E402
from qnx_tests.registry import TestModule  # noqa: E402


# ---------------------------------------------------------------------------
# Module selection
# ---------------------------------------------------------------------------

def _add_module_flags(parser: argparse.ArgumentParser) -> None:
    """Add the ``--<module>`` flags from the registry."""
    for key in MODULES:
        parser.add_argument(
            f"--{key}",
            action="store_true",
            help=f"run the {key} module: {MODULES[key].description}",
        )
    parser.add_argument(
        "--all", action="store_true",
        help="run every registered module in sequence (single QEMU session)",
    )
    parser.add_argument(
        "--list", action="store_true",
        help="list registered modules and exit",
    )


def _selected_modules(args: argparse.Namespace) -> list:
    """Return the list of ``TestModule`` instances the user asked for.

    The historical default (``qnx_run_test.sh``) was to run
    ``base_unittests`` when no module flag was given; we preserve that
    here so existing CI commands keep working.
    """
    if args.list:
        return []
    explicit = [k for k in MODULES if getattr(args, k, False)]
    if args.all:
        return [MODULES[k] for k in MODULES]
    if explicit:
        return [MODULES[k] for k in explicit]
    # Default: base (backward compatible with qnx_run_test.sh)
    return [MODULES["base"]]


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="qnx-tests",
        description=(
            "Run CEF/Chromium tests on QNX under QEMU, optionally one module "
            "at a time.  See docs/qnx/fixes-and-decisions.md section 42."
        ),
    )

    # Module selection
    _add_module_flags(p)

    # gtest filter (positional, kept for backward compat with the old
    # qnx_run_test.sh UX)
    p.add_argument(
        "filter", nargs="?", default="",
        help="GTest filter (e.g. 'ProcessTest.*' or '*'); "
             "applied to the first selected module",
    )
    p.add_argument(
        "--filter", dest="filter_kw", default="",
        help="explicit form of the gtest filter",
    )

    # Backward-compat options carried over from qnx_run.sh
    p.add_argument("--timeout", type=int, default=None,
                   help="per-test/command timeout in seconds")
    p.add_argument("--boot-timeout", type=int, default=None,
                   help="QEMU boot/login timeout in seconds")
    p.add_argument("--serial-port", type=int, default=None,
                   help="TCP serial port for QEMU")
    p.add_argument("--keep-qemu", action="store_true",
                   help="leave QEMU running after the run finishes")
    p.add_argument("--mount-only", action="store_true",
                   help="boot QEMU and mount NFS, then exit")
    p.add_argument("--kill-existing", action="store_true",
                   help="kill any stale qemu-system-x86_64 first")
    p.add_argument("--cmd",
                   help="run an arbitrary guest command (legacy qnx_run.sh UX)")

    # Per-module options
    p.add_argument("--binary", default="",
                   help="override the module's binary (e.g. 'url_unittests')")
    p.add_argument("--stack-size", type=int, default=0,
                   help="pass --stack-size=<KB> to each per-test invocation")
    p.add_argument("--skip-death-tests", action="store_true",
                   help="filter out *DeathTest* tests in per_test mode")
    p.add_argument("--dry-run", action="store_true",
                   help="list tests and exit (per_test modules only)")
    p.add_argument("--max-tests", type=int, default=0,
                   help="limit the number of per_test invocations")
    p.add_argument("--no-default-exclusions", action="store_true",
                   help="do not apply the module's default_exclusions")

    return p


# ---------------------------------------------------------------------------
# Result reporting
# ---------------------------------------------------------------------------

def _print_module_summary(name: str, results) -> int:
    failed = [r for r in results if r[1] != 0]
    print()
    print("=" * 60)
    print(f"[{name}] results: {len(results)} tests, "
          f"{len(results) - len(failed)} PASS, {len(failed)} FAIL")
    for label, ec, dt in failed[:20]:
        print(f"  FAIL: {label} (exit {ec}, {dt:.1f}s)")
    if len(failed) > 20:
        print(f"  ... and {len(failed) - 20} more failures")
    return 0 if not failed else 1


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv=None) -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    tools = os.path.dirname(here)
    parser = build_parser()
    args = parser.parse_args(argv)

    # Build config from env
    cfg = QNXConfig.from_env(tools)
    if args.serial_port is not None:
        cfg.serial_port = args.serial_port
    if args.boot_timeout is not None:
        cfg.boot_timeout = args.boot_timeout
    if args.kill_existing:
        cfg.kill_existing = True
    if args.keep_qemu or args.mount_only:
        cfg.keep_qemu = True

    # --list: print module table and exit
    if args.list:
        print("Registered test modules:")
        for k, m in MODULES.items():
            print(f"  --{k:<12} {m.description}")
        print(f"  --all           run every module in sequence")
        return 0

    modules = _selected_modules(args)
    if not modules:
        parser.print_help()
        return 2

    # --cmd is a single arbitrary guest command (legacy qnx_run.sh UX)
    if args.cmd:
        modules = [_CmdModule(args.cmd, cfg.cmd_timeout)]

    # Apply per-test options
    for m in modules:
        if args.timeout is not None:
            m.default_timeout = args.timeout
            m.default_batch_timeout = args.timeout
        if args.binary:
            m.binary = args.binary
        if args.stack_size:
            m.per_test_args = list(m.per_test_args) + [
                f"--stack-size={args.stack_size}"
            ]

    # --mount-only path
    if args.mount_only:
        qemu_pid = boot_qemu(cfg)
        try:
            stamp = time.strftime("%Y%m%d_%H%M%S")
            serial_log = os.path.join(cfg.build_dir,
                                      f"qnx_mount_{stamp}.serial.log")
            serial = QNXSerial(cfg.serial_port, cfg.boot_timeout, serial_log)
            serial.connect()
            serial.boot_and_login()
            # Pick a sensible primary binary for the initial env setup.
            # Multi-binary groups have an empty modules[0].binary, so we
            # fall back to the first BinarySpec name. The TestModule.run()
            # call later re-exports CHROME_EXE_PATH per binary.
            primary = (
                modules[0].binary
                or modules[0].effective_binaries()[0].name
            )
            serial.setup_env(cfg.guest_build_dir(), primary)
            print("\n=== QEMU kept running; NFS mounted. ===")
            print(f"Attach serial: socat -,raw,echo=0 TCP:127.0.0.1:{cfg.serial_port}")
            print(f"Stop QEMU: kill {qemu_pid}")
        finally:
            if not cfg.keep_qemu:
                kill_qemu(qemu_pid)
        return 0

    # Normal run path: boot QEMU once, share the session across modules
    qemu_pid = boot_qemu(cfg)
    overall_failed = 0
    serial: QNXSerial | None = None
    try:
        # Open the first serial session; reuse for all modules in this run
        stamp = time.strftime("%Y%m%d_%H%M%S")
        serial_log = os.path.join(cfg.build_dir, f"qnx_{stamp}.serial.log")
        serial = QNXSerial(cfg.serial_port, cfg.boot_timeout, serial_log)
        serial.connect()
        serial.boot_and_login()
        # Pick a sensible primary binary for the initial env setup.
        # Multi-binary groups have an empty modules[0].binary, so we
        # fall back to the first BinarySpec name. The TestModule.run()
        # call later re-exports CHROME_EXE_PATH per binary.
        primary = (
            modules[0].binary
            or modules[0].effective_binaries()[0].name
        )
        serial.setup_env(cfg.guest_build_dir(), primary)

        for m in modules:
            # --skip-death-tests (per_test only)
            if args.skip_death_tests and m.strategy == "per_test":
                # Implemented as a post-list filter
                orig_list_tests = m.list_tests
                def _filtered(serial_, g, timeout, _orig=orig_list_tests):
                    return [n for n in _orig(serial_, g, timeout)
                            if "DeathTest" not in n]
                m.list_tests = _filtered  # type: ignore[method-assign]

            # --no-default-exclusions
            if args.no_default_exclusions:
                m.default_exclusions = []

            # --dry-run
            if args.dry_run and m.strategy == "per_test":
                tests = m.list_tests(serial, cfg.guest_build_dir(), timeout=120)
                for t in tests[:50]:
                    print(t)
                if len(tests) > 50:
                    print(f"... and {len(tests) - 50} more")
                continue

            # --max-tests cap
            if args.max_tests and m.strategy == "per_test":
                orig_run = m._run_per_test
                def _capped(cfg_, serial_, g, f, _orig=orig_run, cap=args.max_tests):
                    failed, results = _orig(cfg_, serial_, g, f)
                    return failed, results[:cap]
                m._run_per_test = _capped  # type: ignore[method-assign]

            # Run
            filter_arg = args.filter_kw or args.filter or ""
            failed, results = m.run(cfg, serial, cfg.guest_build_dir(), filter_arg)
            overall_failed += _print_module_summary(m.name, results)
    finally:
        if serial is not None:
            serial.close()
        if not cfg.keep_qemu:
            kill_qemu(qemu_pid)

    return 0 if overall_failed == 0 else 1


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class _CmdModule(TestModule):
    """Synthetic module that runs an arbitrary guest command exactly once.

    Used to preserve the legacy ``qnx_run_test.sh --cmd '<guest cmd>'`` UX.
    """

    def __init__(self, cmd: str, timeout: int):
        super().__init__(
            name="cmd",
            description="(arbitrary guest command)",
            binary="base_unittests",
            strategy="single",
            default_timeout=600,
            default_batch_timeout=timeout or 1800,
        )
        self._cmd = cmd

    def run(
        self,
        cfg: QNXConfig,
        serial: QNXSerial,
        guest_build_dir: str,
        cli_filter: str = "",
    ):
        t0 = time.time()
        ec, _ = serial.run_command(self._cmd, timeout=self.default_batch_timeout)
        elapsed = time.time() - t0
        results = [("cmd", ec, elapsed)]
        return (0 if ec == 0 else 1, results)


if __name__ == "__main__":
    sys.exit(main())
