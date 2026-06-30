# qnx-ceftests-cors-single-process-crashes

- Stage: test
- Category: runtime-assumption
- First observed: 2026-06-29
- Affected target: `ceftests` on QNX, single-process broad run / `CorsTest.*`

## Signatures

- `CorsTest.IframeNoneServerToServer:CorsTest.IframeAllowScriptsAndSameOriginServerToServer`
  exited with `139` / `SIGSEGV`.
- QNX gdb core top frame:
  - `GetPathURL()` at `cef/tests/ceftests/cors_unittest.cc`
- After fixing that crash, `CorsTest.*` later exited with `136` / `SIGFPE`.
- Signal probe plus `pidin mem` showed the `SIGFPE` PC inside `libcef.so`.
- Symbolized offset:
  - `performance_manager::MetricsProviderCommon::RecordAvailableMemoryMetrics()`
  - `chrome/browser/performance_manager/metrics/metrics_provider_common.cc:78`
- Faulting expression divided by `base::SysInfo::AmountOfPhysicalMemory()` because QNX returned total physical memory as 0.

## Root causes

1. `CorsTestHandler::DestroyTest()` assumed it was called during the normal
   `shutting_down_` path and on the UI thread. Timeout/failure cleanup could
   arrive from a different state/thread, leaving the embedded test server and
   setup partially alive. The next `CorsTest` could then access stale test
   state and crash in URL construction.
2. `CookieTestSetup::VerifyClearedCookies()` checked that exactly one cookie was
   present with `EXPECT_EQ`, but continued to index `cookies[0]` even when the
   vector was empty after a failed/timeout path.
3. QNX `sysconf(_SC_PHYS_PAGES)` and `_SC_AVPHYS_PAGES` can return `-1` with
   `errno == 0` on QEMU. `SysInfo::AmountOfTotalPhysicalMemoryImpl()` converted
   this to 0, and periodic memory telemetry eventually divided by zero.

## Fix

- Make `CorsTestHandler::DestroyTest()` post to `TID_UI` when needed. If called
  before `shutting_down_` is set, mark shutdown and stop the server, then let the
  normal shutdown callback continue cleanup.
- Make `CookieTestSetup::VerifyClearedCookies()` return false before reading
  `cookies[0]` when the cookie count is not exactly one.
- Make QNX `SysInfo::AmountOfTotalPhysicalMemoryImpl()` fall back to summing
  syspage `asinfo` `sysram` ranges via `walk_asinfo()` when `_SC_PHYS_PAGES`
  is unavailable.

## Verification

- Rebuilt `ceftests` successfully with `out/qnx_release/ninja_qnx.sh ceftests`.
- `CorsTest.*` single-process run completed all 297 tests without `SIGSEGV`,
  `SIGFPE`, or abort:
  - `297 tests from CorsTest`
  - `271 PASSED`, `26 FAILED`
  - process exit `1` only due expected test assertion failures.
- Broad single-process `ceftests` progressed past `CorsTest.*` without signal
  crashes; the remaining broad run blocker is long runtime / many assertion
  timeouts, not the previous CorsTest process crash.

## Notes

- Do not solve this by making QNX `ceftests` default to one-test-per-process;
  that masks the single-process harness bugs.
- `qnx_run_test.sh --cmd` may print wrapper `PASS` for the shell command; use the
  explicit `echo ...:$?` marker from the test process for real gtest status.
