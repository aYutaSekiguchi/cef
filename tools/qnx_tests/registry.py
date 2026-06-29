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
    #: Optional TMPDIR base path; absolute or relative to the guest build dir.
    temp_dir_relpath: str = ""
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
    #: to drive ``v8_unittests`` (see build-error-index.md and the structured notes).
    one_test_per_process: bool = False
    #: Optional TMPDIR base path; absolute or relative to the guest build dir.
    #: Useful for tests that need short, isolated temp paths in a long QEMU
    #: session.
    temp_dir_relpath: str = ""
    #: Optional cap used by --max-tests for per_test runs.
    max_tests: int = 0
    #: Unique suffix for per-run temporary directories.
    temp_run_id: str = field(default_factory=lambda: str(int(time.time())))
    #: Counter used to keep per-test TMPDIR paths short and unique.
    temp_counter: int = 0

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
            temp_dir_relpath=spec.temp_dir_relpath or self.temp_dir_relpath,
            max_tests=self.max_tests,
            temp_run_id=self.temp_run_id,
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
        extra = " ".join(self.per_test_args)
        cmd = (
            f"cd {guest_build_dir} && "
            f"{self._env_prefix(guest_build_dir, clean=True, suffix='_list')}"
            f"./{self.binary} {extra} --gtest_list_tests 2>&1"
        )
        ec, raw = serial.run_command(cmd, timeout=timeout)
        if ec != 0:
            raise RuntimeError(
                f"{self.binary} --gtest_list_tests failed with exit {ec}"
            )
        return _parse_gtest_list(raw)

    def _temp_dir(self, guest_build_dir: str, suffix: str = "") -> str:
        if not self.temp_dir_relpath:
            return ""
        base = (self.temp_dir_relpath if self.temp_dir_relpath.startswith("/")
                else f"{guest_build_dir}/{self.temp_dir_relpath}")
        temp_dir = f"{base}/{self.temp_run_id}"
        if suffix:
            temp_dir = f"{temp_dir}/{suffix}"
        return temp_dir

    def _temp_suffix(self, name: str) -> str:
        self.temp_counter += 1
        return f"t{self.temp_counter:04d}"

    def _env_prefix(self,
                    guest_build_dir: str,
                    clean: bool = False,
                    suffix: str = "") -> str:
        temp_dir = self._temp_dir(guest_build_dir, suffix)
        if not temp_dir:
            return ""
        if clean:
            return f"rm -rf {q(temp_dir)} && mkdir -p {q(temp_dir)} && TMPDIR={q(temp_dir)} "
        return f"mkdir -p {q(temp_dir)} && TMPDIR={q(temp_dir)} "

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
            f"{self._env_prefix(guest_build_dir, suffix=self._temp_suffix(test_filter))}"
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

        QNX executable-path lookup normally uses ``/proc/self/exefile`` now,
        so the runner intentionally keeps ``CHROME_EXE_PATH`` unset instead
        of rewriting it for each binary.
        """
        all_results: List[Tuple[str, int, float]] = []
        total_failures = 0
        for spec in self.effective_binaries():
            label = spec.description or spec.name
            print(f"\n=== {self.name}: running {label} ({spec.name}) ===")
            sub = self._resolve(spec)
            failed, results = sub._run_with_strategy(
                cfg, serial, guest_build_dir, cli_filter
            )
            total_failures += failed
            all_results.extend(results)
        return total_failures, all_results


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

        if self.max_tests:
            before = len(tests)
            tests = tests[: self.max_tests]
            print(f"After --max-tests {self.max_tests}: {len(tests)} tests "
                  f"(from {before})")

        self._cleanup_after_invocation(serial, guest_build_dir)

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
            # CEF/browser tests can leave renderer/browser child processes
            # behind even when the main gtest process exits. Kill the test
            # binary after every per-test invocation so stale children cannot
            # accumulate and poison later tests in the same QEMU session.
            self._cleanup_after_invocation(serial, guest_build_dir)

        failed = sum(1 for _, ec, _ in results if ec != 0)
        return failed, results

    def _cleanup_after_invocation(
        self,
        serial: QNXSerial,
        guest_build_dir: str,
    ) -> None:
        cleanup = f"slay -f -9 {self.binary} >/dev/null 2>&1 || true"
        serial.run_command(cleanup, timeout=15)


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
        if stripped.startswith("__PI_QNX_EXIT__:"):
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
