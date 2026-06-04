"""Test module base class + registry.

Each test module under ``qnx_tests/modules/`` is a ``TestModule`` subclass
that declares:

* the guest binary it drives
* the launch strategy (``single`` runs the binary once with a gtest
  filter; ``per_test`` lists tests first and runs the binary once per
  test -- this is what sidesteps V8's ``WithDefaultPlatformMixin`` issue)
* default exclusions and per-test arguments

The registry maps the ``--<module>`` CLI flag to the module class.  Adding
a new test target therefore means:

1.  dropping a new file under ``qnx_tests/modules/``
2.  adding one import + one line to ``MODULES`` below

No other glue is needed.
"""

from __future__ import annotations

import os
import re
import sys
import time
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

from .common import QNXConfig, QNXSerial


# ---------------------------------------------------------------------------
# Base class
# ---------------------------------------------------------------------------

@dataclass
class BinarySpec:
    """Per-binary configuration for a test module.

    A ``TestModule`` can be either a single binary (legacy: just set
    ``binary``) or a group of binaries (``binaries=[...]``). When the
    group form is used, each ``BinarySpec`` can override the module
    defaults for strategy, timeouts, exclusions, per-test args, etc.
    """

    #: Guest binary, relative to ``BUILD_DIR``.
    name: str
    #: Optional human-readable label for ``--list`` output.
    description: str = ""
    #: ``single`` (default) or ``per_test``. Falls back to module default.
    strategy: Optional[str] = None
    #: Per-binary batch timeout in seconds. Falls back to module default.
    default_batch_timeout: Optional[int] = None
    #: Per-test timeout in seconds. Falls back to module default.
    default_timeout: Optional[int] = None
    #: gtest exclusion patterns for this binary. Falls back to module.
    default_exclusions: List[str] = field(default_factory=list)
    #: Extra args appended to each invocation. Falls back to module.
    per_test_args: List[str] = field(default_factory=list)
    #: Whether to use ``--gtest_filter=<single>`` for per_test mode.
    one_test_per_process: Optional[bool] = None
    #: Whether to parse the V8-style unittests.status file for [SKIP].
    parse_status_file: Optional[bool] = None
    #: Status file path relative to ``CHROMIUM_SRC`` (only when
    #: ``parse_status_file`` is true).
    status_file_relpath: str = ""


@dataclass
class TestModule:
    """Declarative description of a runnable test target.

    A module is either a single binary (set ``binary``) or a group of
    binaries (set ``binaries=[...]``).  The group form is what the
    ``--swiftshader`` test group uses: it runs ``system_unittests``,
    ``reactor_llvm_unittests`` and ``reactor_subzero_unittests``
    sequentially.
    """

    #: Flag used on the CLI, e.g. ``--base``.
    name: str
    #: Human-readable description, shown in ``--list``.
    description: str
    #: Guest binary, relative to ``BUILD_DIR``.  Mutually exclusive with
    #: ``binaries``; when ``binaries`` is set, this is unused.
    binary: str = ""
    #: List of binaries to run sequentially as a test group.  When set,
    #: ``binary`` is ignored.  Each entry can override the module-level
    #: defaults for strategy, timeouts, exclusions, etc.
    binaries: List[BinarySpec] = field(default_factory=list)
    #: ``single`` -> run binary once with ``--gtest_filter``;
    #: ``per_test`` -> run binary once per test.
    strategy: str = "single"
    #: Default per-test (or per-batch) timeout in seconds.
    default_timeout: int = 600
    #: Broad-run default timeout; ``single`` strategy uses this.
    default_batch_timeout: int = 7200
    #: gtest exclusion patterns applied to the broad-run filter.
    default_exclusions: List[str] = field(default_factory=list)
    #: Extra args appended to each binary invocation
    #: (e.g. ``v8_unittests``'s ``--stack-size=384``).
    per_test_args: List[str] = field(default_factory=list)
    #: Whether to parse ``v8/test/unittests/unittests.status`` for
    #: ``[SKIP]`` annotations and apply them to the test list.
    parse_status_file: bool = False
    #: Path of the status file relative to ``CHROMIUM_SRC``; only used
    #: when ``parse_status_file`` is true.
    status_file_relpath: str = ""
    #: If True, ``per_test`` mode passes ``--gtest_filter=<single>`` so
    #: only one test runs per binary invocation.  This is the only way
    #: to drive ``v8_unittests`` (see fixes-and-decisions.md section 26).
    one_test_per_process: bool = False

    def __post_init__(self) -> None:
        if not self.binary and not self.binaries:
            raise ValueError(
                f"TestModule {self.name!r} must set either 'binary' or 'binaries'"
            )
        if self.binary and self.binaries:
            raise ValueError(
                f"TestModule {self.name!r} sets both 'binary' and 'binaries'; "
                "use one or the other"
            )

    def effective_binaries(self) -> List[BinarySpec]:
        """Return the list of binaries to run, normalising single-binary
        modules to a 1-element list of ``BinarySpec``.
        """
        if self.binaries:
            return self.binaries
        return [BinarySpec(name=self.binary)]

    def _resolve(self, spec: BinarySpec) -> "TestModule":
        """Build a single-binary ``TestModule`` view of ``spec``.

        Used to reuse the existing ``_run_single`` / ``_run_per_test``
        drivers without copy-paste.  The returned module shares the
        serial/QNXConfig environment but carries ``spec``'s overrides.
        """
        return TestModule(
            name=self.name,
            description=self.description,
            binary=spec.name,
            strategy=spec.strategy or self.strategy,
            default_timeout=spec.default_timeout or self.default_timeout,
            default_batch_timeout=(
                spec.default_batch_timeout or self.default_batch_timeout
            ),
            default_exclusions=(
                spec.default_exclusions
                if spec.default_exclusions
                else self.default_exclusions
            ),
            per_test_args=(
                spec.per_test_args
                if spec.per_test_args
                else self.per_test_args
            ),
            parse_status_file=(
                spec.parse_status_file
                if spec.parse_status_file is not None
                else self.parse_status_file
            ),
            status_file_relpath=(
                spec.status_file_relpath or self.status_file_relpath
            ),
            one_test_per_process=(
                spec.one_test_per_process
                if spec.one_test_per_process is not None
                else self.one_test_per_process
            ),
        )

    # The following are filled in by ``run()`` and ``_run_*()``.

    def list_tests(
        self, serial: QNXSerial, guest_build_dir: str, timeout: int
    ) -> List[str]:
        """Return the list of fully-qualified test names for this module.

        The default implementation calls ``<binary> --gtest_list_tests``
        and parses the GTest output.  Modules that need a different
        enumeration step (e.g. cctest) can override this.
        """
        cmd = f"cd {guest_build_dir} && ./{self.binary} --gtest_list_tests 2>&1"
        ec, raw = serial.run_command(cmd, timeout=timeout)
        if ec != 0:
            raise RuntimeError(
                f"{self.binary} --gtest_list_tests failed with exit {ec}"
            )
        return _parse_gtest_list(raw)

    def build_invocation(
        self,
        guest_build_dir: str,
        test_filter: str,
        timeout: int,
    ) -> str:
        """Compose the guest-side command line for a single invocation.

        For ``single`` strategy this is the whole batch command.
        For ``per_test`` this is the command for a single test.
        """
        extra = " ".join(self.per_test_args)
        return (
            f"cd {guest_build_dir} && "
            f"./{self.binary} {extra} --gtest_filter={q(test_filter)} 2>&1"
        )

    # ----- per-strategy drivers -------------------------------------------

    def run(
        self,
        cfg: QNXConfig,
        serial: QNXSerial,
        guest_build_dir: str,
        cli_filter: str = "",
    ) -> Tuple[int, List[Tuple[str, int, float]]]:
        """Run this module and return ``(total_failures, results)``.

        For multi-binary groups (``--swiftshader`` style) this iterates
        over each ``BinarySpec`` and aggregates the per-binary results.
        A failure in any binary fails the whole group.

        CHROME_EXE_PATH is reset for every binary so that QNX
        ``base_paths_posix.cc`` resolves ``icudtl.dat`` (and DIR_ASSETS)
        to the right place; this is what makes
        ``swiftshader_reactor_llvm_unittests`` etc. find the ICU data
        file the first binary's path would not point to.
        """
        all_results: List[Tuple[str, int, float]] = []
        total_failures = 0
        for spec in self.effective_binaries():
            label = spec.description or spec.name
            print(f"\n=== {self.name}: running {label} ({spec.name}) ===")
            self._set_chrome_exe_path(serial, guest_build_dir, spec.name)
            sub = self._resolve(spec)
            failed, results = sub._run_with_strategy(
                cfg, serial, guest_build_dir, cli_filter
            )
            total_failures += failed
            all_results.extend(results)
        return total_failures, all_results

    @staticmethod
    def _set_chrome_exe_path(
        serial: QNXSerial, guest_build_dir: str, binary: str
    ) -> None:
        """Re-export ``CHROME_EXE_PATH`` on the guest for the next binary.

        The initial ``setup_env()`` call in ``cli.py`` exports
        ``CHROME_EXE_PATH`` for the first binary only; in a group that
        is empty, which makes ``base::TestSuite::InitializeICUForTesting``
        fail with ``Invalid file descriptor to ICU data received`` for
        every subsequent binary.  Re-exporting here fixes the group
        case without changing the single-binary happy path.
        """
        cmd = f"export CHROME_EXE_PATH={guest_build_dir}/{binary}"
        serial.run_command(cmd, timeout=15)

    def _run_with_strategy(
        self,
        cfg: QNXConfig,
        serial: QNXSerial,
        guest_build_dir: str,
        cli_filter: str,
    ) -> Tuple[int, List[Tuple[str, int, float]]]:
        """Dispatch on the effective strategy for this single-binary view."""
        if self.strategy == "single":
            return self._run_single(cfg, serial, guest_build_dir, cli_filter)
        if self.strategy == "per_test":
            return self._run_per_test(cfg, serial, guest_build_dir, cli_filter)
        raise ValueError(f"unknown strategy: {self.strategy}")

    def _run_single(
        self,
        cfg: QNXConfig,
        serial: QNXSerial,
        guest_build_dir: str,
        cli_filter: str,
    ) -> Tuple[int, List[Tuple[str, int, float]]]:
        effective = _apply_default_exclusions(cli_filter or "*", self.default_exclusions)
        cmd = self.build_invocation(guest_build_dir, effective, cfg.cmd_timeout)
        t0 = time.time()
        ec, _ = serial.run_command(cmd, timeout=self.default_batch_timeout)
        elapsed = time.time() - t0
        results = [(effective or "*", ec, elapsed)]
        return (0 if ec == 0 else 1, results)

    def _run_per_test(
        self,
        cfg: QNXConfig,
        serial: QNXSerial,
        guest_build_dir: str,
        cli_filter: str,
    ) -> Tuple[int, List[Tuple[str, int, float]]]:
        # 1. List tests
        print(f"\n=== {self.name}: listing tests via --gtest_list_tests ===")
        all_tests = self.list_tests(serial, guest_build_dir, timeout=120)
        print(f"Found {len(all_tests)} tests total")

        # 2. Apply CLI filter
        tests = _filter_tests(all_tests, cli_filter) if cli_filter else all_tests
        if cli_filter:
            print(f"After --filter '{cli_filter}': {len(tests)} tests")

        # 3. Apply status-file [SKIP] annotations
        if self.parse_status_file and self.status_file_relpath:
            skipped = _load_unconditional_skips(
                cfg.chromium_src, self.status_file_relpath
            )
            if skipped:
                compiled = [re.compile(p.replace("*", ".*")) for p in skipped]
                before = len(tests)
                tests = [
                    t for t in tests
                    if not any(s.fullmatch(t) or s.search(t) for s in compiled)
                ]
                print(f"After unittests.status [SKIP]: {len(tests)} tests "
                      f"(removed {before - len(tests)})")

        # 4. Run each test
        results: List[Tuple[str, int, float]] = []
        for idx, name in enumerate(tests, 1):
            print(f"\n[{idx}/{len(tests)}] {name}")
            t0 = time.time()
            cmd = self.build_invocation(guest_build_dir, name, self.default_timeout)
            ec, _ = serial.run_command(cmd, timeout=self.default_timeout)
            elapsed = time.time() - t0
            results.append((name, ec, elapsed))
            status = "PASS" if ec == 0 else f"FAIL (exit {ec})"
            print(f"  {status} ({elapsed:.1f}s)")

        failed = sum(1 for _, ec, _ in results if ec != 0)
        return failed, results


# ---------------------------------------------------------------------------
# Helpers shared by all modules
# ---------------------------------------------------------------------------

def q(s: str) -> str:
    """Single-quote a string for embedding inside ``sh -c '...'``."""
    return "'" + s.replace("'", "'\\''") + "'"


def _apply_default_exclusions(filter_str: str, exclusions: List[str]) -> str:
    """Append the default exclusion list to a gtest filter.

    Replicates the historical ``QNX_ENV_EXCLUSIONS`` logic in
    ``qnx_run_test.sh``:
    * if the user already supplied ``:-``, leave the filter untouched
    * if the filter is the bare ``*``, expand to ``*:-EXCLUSIONS``
    * otherwise append ``:-EXCLUSIONS`` to the user filter
    """
    if not exclusions:
        return filter_str
    # gtest's filter syntax is `POSITIVE:POSITIVE:-NEGATIVE:NEGATIVE:...`.
    # The exclusion list itself is colon-separated *negative* patterns.
    excl = ":".join(exclusions)
    if ":-" in filter_str:
        return filter_str
    if filter_str == "*":
        return f"*:-{excl}"
    return f"{filter_str}:-{excl}"


def _parse_gtest_list(raw: bytes) -> List[str]:
    """Parse ``--gtest_list_tests`` output into fully-qualified test names.

    Format::

        SuiteA.
          Test1
          Test2
        SuiteB.
          ParamTest/0  # GetParam() = 0
    """
    tests: List[str] = []
    current = ""
    for line in raw.decode("utf-8", errors="replace").splitlines():
        # Strip ANSI escape sequences
        line = re.sub(r"\x1b\[[0-9;]*[a-zA-Z]", "", line)
        line = re.sub(r"\x1b\?[0-9;]*[a-zA-Z]", "", line)
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.endswith(".") and not stripped.startswith(" "):
            current = stripped.rstrip(".")
        elif current and stripped and not stripped.startswith("#"):
            test = stripped.split("#")[0].strip()
            if test:
                tests.append(f"{current}.{test}")
    return tests


def _filter_tests(tests: List[str], filter_str: str) -> List[str]:
    """Apply a GTest-style filter to a flat list of test names."""
    if not filter_str:
        return tests
    patterns = []
    for part in filter_str.split(":"):
        part = part.strip()
        if not part:
            continue
        patterns.append(re.compile(part.replace("*", ".*")))
    return [t for t in tests if any(p.search(t) for p in patterns)]


def _load_unconditional_skips(
    chromium_src: str, relpath: str
) -> List[str]:
    """Parse a V8-style ``unittests.status`` file for ``[SKIP]`` patterns.

    Returns patterns that are unconditionally skipped (i.e. listed
    outside any conditional block), plus patterns that are skipped
    under a ``system == qnx`` block.  Mirrors the logic in the
    historical ``qnx_run_v8_unittests.py``.
    """
    path = os.path.join(chromium_src, relpath)
    if not os.path.exists(path):
        print(f"WARNING: status file not found at {path}", file=sys.stderr)
        return []
    with open(path) as fp:
        text = fp.read()

    patterns: List[str] = []
    patterns.extend(_extract_unconditional(text, r"\[ALWAYS,\s*\{\n(.*?)\n\}\]"))
    patterns.extend(_extract_unconditional(
        text, r"\['system == qnx',\s*\{\n(.*?)\n\}\]"
    ))
    return list(dict.fromkeys(patterns))


def _extract_unconditional(text: str, section_regex: str) -> List[str]:
    m = re.search(section_regex, text, re.DOTALL)
    if not m:
        return []
    patterns: List[str] = []
    body = m.group(1)
    for line in body.split("\n"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m2 = re.match(r"'([^']+)'\s*:\s*\[(.*)]", line)
        if not m2:
            continue
        name = m2.group(1)
        body2 = m2.group(2)
        # Split status tokens at the top level (not inside nested brackets)
        depth = 0
        outer: List[str] = []
        current = ""
        for ch in body2:
            if ch == "[":
                depth += 1
            elif ch == "]":
                depth -= 1
            elif ch == "," and depth == 0:
                outer.append(current.strip())
                current = ""
                continue
            if depth == 0:
                current += ch
        if current.strip():
            outer.append(current.strip())
        is_skip = "SKIP" in outer
        is_pass = "PASS" in outer
        if is_skip and not is_pass:
            patterns.append(name)
    return patterns
