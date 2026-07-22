# qnx-ceftests-axviewportcollapse-zero-viewport-bounds

- Stage: test
- Category: runtime-assumption
- First observed: 2026-06-30
- Affected target: `ceftests` on QNX, `AxViewportCollapseTest.*`

## Signatures

After the Skia allocator crash was fixed, these QNX headless tests failed with
expectation mismatches instead of crashing:

- `AxViewportCollapseTest.CollapseEnabled`
  - missing `In Viewport Heading`
  - missing `Visible Button`
- `AxViewportCollapseTest.FixedPositionVisible`
  - missing `Fixed Button`
- `AxViewportCollapseTest.AllVisible`
  - `Site Nav` had empty `childIds`
- `AxViewportCollapseTest.ZoomedCollapse`
  - `Edge Nav` had empty `childIds`
  - missing `Edge Link` / `Top Content`
- `AxViewportCollapseTest.AgenticWorkflow`
  - `Main Navigation` had empty `childIds`

No signal crash was involved.

## Root cause

QNX runs Chrome-style CEF browsers with Ozone headless and no X11/aura host
window. The Chrome wrapper did not forward `RenderViewReady()` to the native
Aura delegate, so `RenderWidgetHostView` never received an initial non-empty
size. Pages loaded with a JavaScript viewport of `0x0`.

After wiring an initial `800x600` size into the view, the remaining failures
were caused by QNX headless AX bounds such as `800x0` or `0x0`: the node origin
was in the viewport, but `PhysicalRect::Intersects()` returned false for empty
rects. Viewport collapse then treated visible landmarks as offscreen summaries
and emptied their `childIds`.

## Fix

- Forward `CefBrowserPlatformDelegateChrome::RenderViewReady()` to the native
  delegate on QNX.
- In `CefBrowserPlatformDelegateNativeLinux` on QNX, set the
  `RenderWidgetHostView` size directly at `RenderViewReady()` and `SizeTo()`.
  This supplies the missing viewport size for Ozone headless without an X11 host
  window.
- In viewport-collapse visibility checks, treat an empty AX bounds rect as
  visible when its origin lies inside the viewport. This preserves the feature
  test semantics: visible zero-height/zero-size AX nodes are serialized; truly
  offscreen nodes remain collapsible.

## Verification

- Rebuilt `ceftests` with `./out/qnx_release/ninja_qnx.sh ceftests`.
- QNX focused run:

```bash
./ceftests --ozone-platform=headless --disable-gpu --disable-gpu-compositing \
  --gtest_filter="AxViewportCollapseTest.*"
```

Result:

- 12 tests ran
- 12 passed
- `AX_ZERO_EC:0`

## Notes

This does not weaken the upstream CEF feature tests. The expected AX nodes,
child IDs, zoom behavior, and scroll workflow are still asserted. The fix makes
QNX headless provide a real viewport and handles QNX's empty AX bounds for
visible nodes.

## Follow-up: Chrome window retained the 800x600 fallback

On 2026-07-23, `cefsimple --use-native --start-maximized` exposed a second
effect of the initial-size workaround. The QNX Screen window, root compositor,
and Chrome UI were all 1280 pixels wide, but the web page layout stopped at
800 pixels and the remaining 480 pixels showed the WebContents background.

The Chrome-style delegate forwards `RenderViewReady()` to the native delegate
after BrowserWindow has already laid out the maximized window. The native QNX
delegate then replaced the correctly laid-out renderer viewport with the
800x600 fallback derived from an empty `CefWindowInfo`. No subsequent window
resize occurred to restore the actual content size.

The Chrome delegate now keeps the native `RenderViewReady()` call so truly
headless browsers still receive a non-empty fallback and the Aura root-window
callback. It then reads `BrowserWindow::GetContentsSize()` and calls the native
delegate's `SizeTo()` when that size is non-empty. This makes the Chrome window
layout authoritative without removing the headless fallback.

Verification:

- `cefsimple` and `ceftests` built successfully with `ninja_qnx.sh`.
- `AxViewportCollapseTest.*`: 12 tests ran and 12 passed on QNX headless.
- The maximized input probe reported `viewport=1280x681`,
  `screen=1280x768`, and `outer=1280x768`.
- The 8-pixel right-edge target occupied `x=1271..1279`; a click at guest
  coordinate `(1275,159)` reached the DOM at content coordinate `(1275,72)`.
