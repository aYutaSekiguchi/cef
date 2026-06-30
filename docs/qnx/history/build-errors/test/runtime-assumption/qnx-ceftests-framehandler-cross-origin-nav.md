# qnx-ceftests-framehandler-cross-origin-nav

- Stage: test
- Category: runtime-assumption
- First observed: 2026-06-30
- Affected target: `ceftests` on QNX, `FrameHandlerTest`

## Signature

- `FrameHandlerTest.OrderSubCrossOriginPeersNavCrossOrigin` and
  `FrameHandlerTest.OrderSubCrossOriginChildrenNavCrossOrigin` time out
  after 10 s (or any multiplier) waiting for cross-origin sub-frame
  callbacks (`OnFrameAttached`, `OnLoadStart`, etc.) after a
  cross-origin main-frame navigation.
- Same-origin peers or no additional navigation pass. The failure is
  specific to the combination "cross-origin sub-frame + cross-origin main
  navigation".
- `OrderSubCrossOriginChildren` (cross-origin sub-frame, no navigation)
  passes in isolation; it only fails when run after other tests in the
  same suite, suggesting state contamination across tests.

## Status

- No durable fix attempted. Increasing the test timeout does not help:
  `ct_fh_t6` (multiplier=6, 30 s) still timed out.
- Likely root cause: Chromium cross-process frame callback ordering on
  QEMU/QNX when both the main frame and the sub-frames live in
  out-of-process renderers spawned by the same navigation. The Blink/CC
  timing that decides which callback fires first differs from the
  reference platform and trips the strict ordering checks in
  `frame_handler_unittest.cc:444`.
- Suggested next steps when revisiting:
  1. Capture a NetLog for the failing navigation and confirm whether the
     sub-frame `OnLoadStart` ever fires on the browser side.
  2. Look for a `--single-process` or `--disable-site-isolation-trials`
     Chromium switch that could let these tests run as a single process
     in `qnx_tests/modules/ceftests.py::per_test_args`.
  3. If the underlying ordering is genuinely different on QNX, the test
     expectations need a QNX-specific override; this would weaken the
     test and should not be done without explicit user sign-off.

## Notes

- The other `FrameHandlerTest.*` tests pass, including
  `OrderSubCrossOriginPeers`, `OrderSubSameOriginPeersNavCrossOrigin`,
  and `OrderSubCrossOriginChildren` (in isolation).
- This note exists so the next agent does not re-discover the failure
  pattern and so the open items list stays accurate.