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
