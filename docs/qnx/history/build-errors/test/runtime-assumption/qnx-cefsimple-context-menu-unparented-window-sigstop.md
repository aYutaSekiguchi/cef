# QNX cefsimple context menu stops an unparented application window

- Date: 2026-07-22
- Signature: `[1]+ Stopped ... __PI_QNX_EXIT__:151` after creating a `PlatformWindowType::kMenu`
- Stage: test
- Category: runtime-assumption
- Scope: QNX Ozone transient windows / Chromium Views context menus

## Symptoms

Right-clicking web content created and rendered the native context-menu window,
then stopped the browser process. The shell reported exit status 151. QNX SDP
8 defines `SIGSTOP` as signal 23, so `128 + 23 = 151`; this was a stopped
process rather than a SIGTRAP or segmentation fault.

The same result occurred in foreground and `qnx_run.sh --detach` launches. All
menu visibility and capture calls returned successfully before the stop.

## Root cause

The QNX Ozone factory discarded `PlatformWindowInitProperties::parent_widget`
and did not advertise that non-top-level windows require explicit parents.
Consequently, a Views menu (`type=2`) reached `QnxWindow` with
`parent_widget=0` and was created as a second `SCREEN_APPLICATION_WINDOW`.

QNX Screen requires dialogs and transient surfaces to be child windows joined
to the owner's window group. Treating the context menu as an independent
application window caused the browser process to be suspended during the
window lifecycle transition. `QnxWindow::Show(bool inactive)` also ignored its
argument and activated menus requested through `MenuHost::ShowInactive()`.

## Fix pattern

- Set `set_parent_for_non_top_level_windows` in the QNX Ozone platform
  properties so Views forwards its context/owner accelerated widget.
- Create parented transient surfaces as `SCREEN_CHILD_WINDOW`.
- Lazily create a QNX Screen window group for the owner and join the child to
  it before showing or allocating buffers.
- Convert the child's global Chromium bounds to coordinates relative to the
  Screen parent.
- Honor `Show(inactive)` and preserve owner activation for menus.

## Applied change

Added the CEF-managed patch
`qnx/chromium/qnx_context_menu_parent_window` after the existing pointer-input
and NativeViewHost forwarding patches.

## Verification

`./out/qnx_release/ninja_qnx.sh cefsimple` completed successfully.

In QEMU virgl, the first right-click logged a menu with
`parent_widget=1 parent_found=1`. The runner remained active instead of
returning 151. Three right-click/open and outside-left-click/dismiss cycles at
different coordinates completed; each cycle created another parented menu,
the main page continued receiving pointer events, and no SIGSTOP, SIGTRAP, or
process exit occurred.

## Files touched

- `patch/patches/qnx/chromium/qnx_context_menu_parent_window.patch`
- `patch/patch.cfg`
- `docs/qnx/status.md`

## Related notes

- `qnx-cefsimple-display-size-pointer-coordinate-mismatch.md`
- `qnx-cefsimple-search-hascapture-trap.md`

