# QNX ceftests per-test runner avoids early suite termination

- Date: 2026-06-28
- Signature: `__PI_QNX_EXIT__:1` before the final gtest summary when running `ceftests --gtest_filter='*'`
- Stage: test
- Category: test-environment
- Scope: ceftests, QNX QEMU runner, temp directories, stale CEF child processes

## Symptoms

A broad QNX run of `ceftests` no longer crashed in `AxViewportCollapseTest.CollapseDefault`, but it still did not execute the full suite. The single-process run stopped mid-stream:

```text
[==========] Running 1949 tests from 81 test suites.
...
[ RUN      ] CorsTest.IframeAllowScriptsCustomStandardSchemeToHttpScheme
__PI_QNX_EXIT__:1
[ceftests] results: 1 tests, 0 PASS, 1 FAIL
```

The failing gtest cases themselves are separate follow-up work; the immediate blocker was that the runner stopped before later tests were invoked.

## Root cause

`ceftests` is not robust as one long-lived broad QNX process. Some failing CEF/browser tests leave process-global CEF state, renderer/browser children, ProcessSingleton files, or timeout state behind. After that, a single broad `ceftests` process can exit before the rest of the gtest list is reached.

A first per-test runner attempt exposed two QNX-specific runner issues:

- timed-out foreground commands left the serial shell in an unsafe state unless interrupted;
- QNX's default `TMPDIR=/data/home/root/tmp` is shared by all invocations and can retain ProcessSingleton/socket leftovers after failures.

Using NFS for `TMPDIR` is not viable because AF_UNIX socket creation/bind fails there. Long nested `TMPDIR` paths are also unsafe for Chromium profile/socket paths on QNX. The runner therefore needs short local qnx6 temp paths.

## Fix pattern

Run `ceftests` using the existing `per_test` strategy:

- list all gtests once;
- invoke `ceftests --gtest_filter=<one test>` for each listed test;
- keep the required QNX headless/GPU-disabled args;
- use a short local per-invocation `TMPDIR` under `/data/home/root/ct/<run>/tNNNN`;
- send Ctrl-C when a command times out so the serial shell is usable for cleanup;
- run `slay -f -9 ceftests` between invocations so stale CEF child processes cannot accumulate;
- make `--max-tests` cap the test list before execution, not after all tests already ran;
- apply `per_test_args` to `--gtest_list_tests` so listing uses the same headless mode as execution;
- ignore `__PI_QNX_EXIT__` marker lines in the gtest-list parser.

## Applied change

- `tools/qnx_tests/modules/ceftests.py`
  - switched from `single` to `per_test` strategy;
  - retained `--ozone-platform=headless --disable-gpu --disable-gpu-compositing`;
  - set short local TMPDIR base `/data/home/root/ct`.
- `tools/qnx_tests/registry.py`
  - added optional per-module TMPDIR handling;
  - added per-run/per-test short temp suffixes;
  - fixed `--max-tests` capping;
  - cleaned stale test binary children between per-test invocations;
  - applied per-test args to gtest listing and filtered exit markers.
- `tools/qnx_tests/common.py`
  - sends Ctrl-C on command timeout before returning to the caller.
- `tools/qnx_tests/cli.py`
  - uses the module-level `max_tests` field instead of a post-run result slice.

## Verification

Syntax:

```bash
python3 -m py_compile \
  tools/qnx_tests/common.py \
  tools/qnx_tests/registry.py \
  tools/qnx_tests/cli.py \
  tools/qnx_tests/modules/ceftests.py
```

Focused runner checks:

```text
Found 1953 tests total
After --max-tests 60: 60 tests (from 1953)
[ceftests] results: 60 tests, 55 PASS, 5 FAIL
```

The focused run reached `[60/60]`, had no `CreateUniqueTempDir` failures, no ProcessSingleton bind failures, and no `slay` prompts.

Full runner check with failures intentionally not fixed yet:

```text
Found 1953 tests total
last progress: [1953/1953] ZipReaderTest.ReadArchive
[ceftests] results: 1953 tests, 470 PASS, 1483 FAIL
```

The full run reached the last listed test. No `CreateUniqueTempDir` failures, ProcessSingleton bind failures, slay prompts, Python tracebacks, or exit-133 crashes were observed in the final verified full run. The remaining failures are test-result failures/timeouts to analyze separately.

## Files touched

- `tools/qnx_tests/cli.py`
- `tools/qnx_tests/common.py`
- `tools/qnx_tests/modules/ceftests.py`
- `tools/qnx_tests/registry.py`

## Related notes

- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ceftests-skia-realloc-cross-dso-allocator.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ceftests-axviewportcollapse-handoff-2026-06-27.md`
