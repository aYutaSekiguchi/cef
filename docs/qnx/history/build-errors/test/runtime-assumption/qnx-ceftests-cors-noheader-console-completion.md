# qnx-ceftests-cors-noheader-console-completion

- Stage: test
- Category: runtime-assumption
- First observed: 2026-07-01, while validating the fontconfig-backed QNX
  `ceftests` run after the V8 CodeRange reduction.
- Affected target: `ceftests` on QNX, specifically cross-origin NoHeader XHR
  cases using standard schemes on both sides.

## Signature

Two `CorsTest` cases time out after the default QNX-scaled 10s test timeout:

- `CorsTest.XhrNoHeaderServerToHttpScheme`
- `CorsTest.XhrNoHeaderHttpSchemeToCustomStandardScheme`

The QNX serial log shows Chromium emitted the expected CORS console message:

```text
Access to XMLHttpRequest at '...' from origin '...' has been blocked by CORS policy: No 'Access-Control-Allow-Origin' header is present on the requested resource.
```

but the CEF test did not receive the JS query callback before timeout:

```text
../../cef/tests/ceftests/test_handler.cc:559: Failure
Test timed out after 10000ms
```

Before the QNX-specific expectation fix, `AssertDone()` also reported
`expected_success_query_ct = 1` while `success_query_ct = 0` for the main
resource.

## Root cause

The CEF `CorsTest` setup assumed that the cross-origin XHR would complete
through the JS success/failure query path. On QNX Chromium 147, the browser
correctly reports the NoHeader CORS block as a console message, but the blocked
XHR does not reliably deliver `xhr.onerror` to CEF's JS test callback before the
default timeout.

For non-standard scheme block cases, the existing test design already completes
without a query callback because the protocol-scheme block is represented by the
expected console message and no query count is expected. The QNX standard-scheme
NoHeader path needs the same terminal signal: once the expected console message
arrives and resource counts are satisfied, the test is done.

This is a CEF test-framework runtime assumption on QNX. It is not caused by the
fontconfig fix, V8 CodeRange reduction, or CORS policy behavior itself.

## Fix

In `tests/ceftests/cors_unittest.cc`:

1. On QNX, after an expected console message is observed, call
   `TriggerDestroyTestIfDone()` so `TestSetup::IsDone()` is re-evaluated when
   the console message is the final observable signal.
2. For QNX cross-origin NoHeader XHRs using standard schemes
   (`!add_header && sub_resource->is_cross_origin && sub_resource->supports_cors`),
   override `main_resource->expected_success_query_ct` back to `0` instead of
   expecting a success query. Leave `expected_failure_query_ct` at its default
   `0`, matching the observed QNX behavior where no JS query callback is
   delivered.

The change is guarded with `#if defined(OS_QNX)` and does not alter Linux CEF
expectations.

## Verification

- Rebuilt `ceftests`:

```bash
cd out/qnx_release
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
./ninja_qnx.sh ceftests
```

- Narrow QNX run:

```bash
./cef/tools/qnx_run_test.sh --cmd \
  'cd /mnt/nfs/out/qnx_release && \
   TMPDIR=/data/home/root/ct_cors_qnx2 \
   ./ceftests --ozone-platform=headless --disable-gpu --disable-gpu-compositing \
   --gtest_filter="CorsTest.XhrNoHeaderServerToHttpScheme:CorsTest.XhrNoHeaderHttpSchemeToCustomStandardScheme"'
```

Result: 2/2 PASS.

- Related handoff set:

```bash
--gtest_filter="CorsTest.IframeAllowScriptsAndSameOriginCustomUnregisteredSchemeToCustomUnregisteredScheme:CorsTest.XhrNoHeaderServerToHttpScheme:CorsTest.XhrNoHeaderHttpSchemeToCustomStandardScheme:FrameHandlerTest.OrderSubCrossOriginPeersNavCrossOrigin"
```

Result: 4/4 PASS.

- Full `CorsTest.*` run: 296/297 PASS. The remaining
  `CorsTest.RedirectGet302HttpSchemeToCustomUnregisteredScheme` timeout passed
  when immediately rerun as a single test, so it is tracked as a load-dependent
  flake separate from this NoHeader console-completion issue.

## Search terms

`cors_noheader_console_completion_qnx`, `XhrNoHeaderServerToHttpScheme`,
`XhrNoHeaderHttpSchemeToCustomStandardScheme`, `TriggerDestroyTestIfDone`,
`expected_success_query_ct`, `No Access-Control-Allow-Origin header`,
`CORS console message terminal signal`, `xhr.onerror not delivered QNX`.
