# qnx-ceftests-cors-http-keepalive-subframe-stall

- Stage: test
- Category: runtime-assumption
- First observed: 2026-06-30
- Affected target: `ceftests` on QNX, `CorsTest.*`

## Signature

- `CorsTest.IframeNoneServerToServer` could fail even when run alone:
  - `Test timed out after 10000ms`
  - iframe resource `expected_response_ct == 1`, `response_ct == 0`
  - missing sandbox console message:
    `Blocked script execution in 'http://127.0.0.1:8098/...iframe.html' because the document's frame is sandboxed and the 'allow-scripts' permission is not set.`
- Full `CorsTest.*` previously stopped early or reported many timeout/count
  mismatches in iframe/redirect cases.
- NetLog for a failing run showed the iframe navigation reused the main-frame
  HTTP/1.1 socket:
  - `SOCKET_POOL_REUSED_AN_EXISTING_SOCKET`
  - `HTTP_TRANSACTION_SEND_REQUEST_HEADERS`
  - then it remained in `HTTP_STREAM_PARSER_READ_HEADERS` until test timeout.
- Test instrumentation showed CEF saw the iframe URL in `GetResourceHandler`,
  but the embedded test server did not receive the request before timeout; when
  teardown raced, the late request could reach `test_server_manager.cc` with an
  empty observer list.

## Root cause

QNX can stall server-backed `CorsTest` subframe/redirect requests when Chromium
reuses the main-frame HTTP/1.1 keep-alive connection to the embedded test
server. The request headers are generated and sent from Chromium's perspective,
but the embedded server callback is not reached before the test timeout.

This is a transport/test-server interaction on QNX, not a CORS expectation
mismatch. The expected resources, request counts, success query counts, and
console messages are still valid.

## Fix

For QNX only, add `Connection: close` to server-backed `CorsTest` responses.
This prevents reuse of the main-frame HTTP/1.1 connection for subsequent
subframe/redirect navigations while preserving the same CORS resources and test
expectations.

## Verification

- Rebuilt successfully:
  - `./out/qnx_release/ninja_qnx.sh ceftests`
- Focused stability:
  - `CorsTest.IframeNoneServerToServer`: timeout/count failure no longer
    reproduced after the `Connection: close` probe; final focused runs without
    instrumentation passed except for one unrelated post-gtest teardown
    `EC:139` flake observed before the full-suite verification.
  - `IframeNone*` group: `25/25` passed, `CORS_IFNONE_GROUP_EC:0`.
  - first 46 iframe tests through
    `CorsTest.IframeAllowScriptsCustomStandardSchemeToCustomUnregisteredScheme`:
    `46/46` passed, `CORS_F47_EC:0`.
- Full CORS suite:
  - `CorsTest.*`: `297/297` passed.
  - `CORS_FULL_CLOSE_EC:0`.
  - no timeout, `SIGSEGV`, `SIGFPE`, abort, or `Trace/BPT trap` in the full run.

## Notes

- Do not fix this by weakening `CorsTest` expectations or by accepting per-test
  process execution as the default; the test passes when the QNX keep-alive
  stall is avoided.
- If similar QNX failures appear, capture a NetLog and check for
  `SOCKET_POOL_REUSED_AN_EXISTING_SOCKET` followed by a long wait in
  `HTTP_STREAM_PARSER_READ_HEADERS` on `127.0.0.1:8098`.
