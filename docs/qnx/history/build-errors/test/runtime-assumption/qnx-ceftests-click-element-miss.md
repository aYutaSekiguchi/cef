# qnx-ceftests-click-element-miss

- Stage: test
- Category: runtime-assumption
- First observed: 2026-06-30
- Affected target: `ceftests` on QNX, `DownloadTest.Clicked*` (10 tests),
  `permission_prompt_unittest.cc`, and other tests that call
  `SendJavaScriptClickEvent`.

## Signature

- `DownloadTest.ClickedRCNone` timed out waiting for `CanDownload`,
  `OnBeforeDownload`, etc.
- The test page `<html><body><a href="...">CLICK ME</a></body></html>`
  produced an anchor `getBoundingClientRect()` of `{x:8,y:23,w:76,h:0}`
  on QNX. `document.elementFromPoint(20, 20)` returned `BODY` instead of
  the `<a>`, so a click dispatched at the intended coordinates hit only
  the body and never navigated.

## Root cause

Two QNX headless layout effects interact with the existing
`SendJavaScriptClickEvent` script:

1. The inline `<a>` reports a `h:0` bounding rect, so the link is a
   horizontal line at `y=23`. The intended click coordinate `(20, 20)`
   lies above that line and falls outside the anchor's box.
2. Even when dispatched on the right element, a synthesized
   `MouseEvent('click')` is untrusted, and Chromium's download flow on
   QNX headless does not promote the navigation to a download in that
   case.

## Fix

Extend `SendJavaScriptClickEvent` on QNX only:

- If the element returned by `document.elementFromPoint` has no `href`,
  fall back to `document.querySelector('a[href]')` and use that anchor.
- Call `element.click()` (programmatic click) instead of dispatching a
  `MouseEvent`, so the browser default action (navigation to the anchor's
  href) fires reliably on QNX headless.

These tests already pass on Linux/Mac/Windows because the default
platform fonts produce a `h>0` anchor box and a trusted user gesture
that reaches Chromium's download path. The QNX branch only changes the
shape of the synthesized click so it can take the same code path.

## Verification

- Rebuilt: `./out/qnx_release/ninja_qnx.sh ceftests`.
- `DownloadTest.*`: 31/31 pass (`CT_DL_ALL_EC:0`), no abnormal signals.
- `AxViewportCollapseTest.*`: 12/12 still pass; no regression from this
  change or the prior font-cache change.

## Notes

- Do not change `SendJavaScriptClickEvent` for Linux/Mac/Windows; the
  existing `MouseEvent` dispatch keeps working there and matches what
  production code expects.
- `DisplayTest.AutoResize` still fails on QNX because its expected
  on-resize sizes are font-specific (50x18 for the bundled platform
  font); with DejaVu the content measures 63 wide. Resolving it requires
  either relaxing the expectation (weakening the test) or shipping a
  platform-specific expected-size table. The test is already disabled
  on Linux for related reasons.