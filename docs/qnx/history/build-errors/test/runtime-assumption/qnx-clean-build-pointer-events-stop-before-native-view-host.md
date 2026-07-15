# Clean QNX CEF build drops pointer events before NativeViewHost

- Date: 2026-07-15
- Signature: `QNX_INPUT: delegate dispatched widget=1` with no `NativeViewHost forward done` and no DOM `[DOWN]`
- Stage: test
- Category: runtime-assumption
- Scope: QNX Screen pointer translation / Aura BrowserWidget / Views NativeViewHost

## Symptoms

A clean QNX CEF build displays page content, and QEMU pointer injection produces `SCREEN_EVENT_POINTER`, but the probe receives no DOM mouse events.

Before the fix:

```text
Screen event: type=POINTER                  9
QNX_INPUT: delegate dispatched widget=1    1
NativeViewHost forward done                 0
[DOWN]                                      0
[WHEEL]                                     0
```

## Root cause

Three independent clean-tree gaps existed:

1. `qnx_pointer_input_dispatch.patch` was stored as six `new file mode` diffs from `/dev/null`. Bootstrap Phase 1 had already copied files with those names from `new_files`, so patcher incorrectly reported `already applied (skipping)`. The resulting `QnxPlatformEventSource` only logged `SCREEN_EVENT_POINTER` and retained the `TODO(qnx): Translate to ui::MouseEvent and dispatch` path.

2. `WindowTreeHost::Create` constructs the cefsimple BrowserWidget root `aura::Window` with a null delegate. Even after QnxWindow dispatch, Aura could not route the event to a target handler. A QNX BrowserNativeWidgetAura subclass must attach itself to the root after host initialization.

3. Chromium normally expects a hosted native window to receive system input directly. On QNX the event instead arrives through the Views tree and stops at `NativeViewHost`. `NativeViewHost::OnMouseEvent` must forward in-bounds events to the hosted Aura child delegate (`RenderWidgetHostViewAura`) after coordinate conversion.

## Fix pattern

- Regenerate `qnx_pointer_input_dispatch.patch` as modifications against the exact Phase 1 `new_files` state, never as `/dev/null` new-file diffs.
- Add `Window::SetDelegate()` that updates both `delegate_` and `target_handler_`.
- Add `BrowserNativeWidgetAuraQnx`, select it in the QNX factory, and list it in `chrome/browser/ui/BUILD.gn`.
- Add a QNX-only `NativeViewHost::OnMouseEvent` forwarding helper.
- Preserve `MouseWheelEvent` while forwarding; copying it as `MouseEvent` slices its wheel offsets and produces invalid DOM deltas.
- Map QNX horizontal/vertical wheel properties to Chromium `Vector2d(x, y)` in the correct order.
- Use `GetLocalBounds()` and convert from the hosted native window to each child, avoiding parent/local coordinate-space mixing.
- Skip non-left press/release and out-of-bounds events to avoid the previously observed native context-menu and redraw-loop crashes.

## Applied change

- Regenerated `qnx_pointer_input_dispatch.patch` for:
  - `qnx_platform_event_source.{cc,h}`
  - `qnx_window.{cc,h}`
  - `qnx_window_manager.{cc,h}`
- Added `qnx_aura_window_set_delegate.patch`.
- Added `qnx_native_view_host_mouse_forwarding.patch`.
- Added `browser_native_widget_aura_qnx.{cc,h}` to `new_files`.
- Updated `browser_native_widget_factory_qnx.cc` and `chrome_browser_ui_views_stubs_is_qnx.patch`.

## Verification

Clean bootstrap:

```text
checkout_rc=0 gclient_rc=0 bootstrap_rc=0
492 patches total (471 applied, 21 skipped, 0 failed)
```

Clean build:

```text
build_rc=0
[57293/57294] SOLINK ./libcef.so
```

QMP absolute pointer + left-click + wheel injection:

```text
QNX_INPUT: delegate dispatched              1
NativeViewHost forward done                 4
DOM [DOWN]                                  1 event (plus console mirror)
DOM [WHEEL]                                 2 events (plus console mirrors), dx=0/dy=±53
pixel_stats nonzero_bytes                   3003968 / 3003968
accepted=true display_ok=true               110
segmentation violation                      0
trace trap                                  0
```

Representative DOM output:

```text
[DOWN] b=0 x=611 y=273 target=zR
[WHEEL] dy=-53 dx=0 x=615 y=365 target=DIV
[WHEEL] dy=53 dx=0 x=615 y=365 target=DIV
```

## Files touched

- `patch/patch.cfg`
- `patch/patches/qnx/chromium/qnx_pointer_input_dispatch.patch`
- `patch/patches/qnx/chromium/qnx_aura_window_set_delegate.patch`
- `patch/patches/qnx/chromium/qnx_native_view_host_mouse_forwarding.patch`
- `patch/patches/qnx/chromium/chrome_browser_ui_views_stubs_is_qnx.patch`
- `patch/qnx/chromium/new_files/chrome/browser/ui/views/frame/browser_native_widget_aura_qnx.{cc,h}`
- `patch/qnx/chromium/new_files/chrome/browser/ui/views/frame/browser_native_widget_factory_qnx.cc`

## Related notes

- `qnx-clean-build-black-uninitialized-test-frame.md`
- `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md`
