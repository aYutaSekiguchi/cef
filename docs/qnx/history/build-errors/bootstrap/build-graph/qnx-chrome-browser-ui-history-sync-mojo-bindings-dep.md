# QNX: add history_sync_optin:mojo_bindings dep to chrome/browser/ui

## Stage

- stage: bootstrap + compile
- category: build-graph / missing mojo header dep
- target: `obj/chrome/browser/ui/ui/signin_view_controller_delegate_views.o`

## Failure signature

```text
../../chrome/browser/ui/webui/signin/history_sync_optin/history_sync_optin_ui.h:12:10: fatal error: 'chrome/browser/ui/webui/signin/history_sync_optin/history_sync_optin.mojom.h' file not found
   12 | #include "chrome/browser/ui/webui/signin/history_sync_optin/history_sync_optin.mojom.h"
```

Recurs on a clean bootstrap (delete `out/qnx_release/...` and re-run
`ninja -C out/qnx_release ...`) because nothing in the build graph forces
the mojo header to be generated before the views translation unit is
compiled.

## Root cause

`signin_view_controller_delegate_views.cc` (compiled inside
`static_library("ui")` in `chrome/browser/ui/BUILD.gn`) transitively
includes `history_sync_optin_ui.h`, which in turn includes the generated
`history_sync_optin.mojom.h`. The mojo header is produced by the
`mojo_bindings` target under
`chrome/browser/ui/webui/signin/history_sync_optin/BUILD.gn`.

The QNX patch that extended the desktop sources block in
`chrome/browser/ui/BUILD.gn` to include QNX (so that
`signin_view_controller_delegate_views.cc` compiles) did not also add the
matching dep on the producing target. On a dirty build the mojo header is
present from a prior run; on a clean build it is not, and the compile
fails before Ninja notices it should be generated.

## Fix

In `chrome/browser/ui/BUILD.gn`, inside the same
`is_win || is_mac || is_linux || is_chromeos || is_qnx` block that already
includes the desktop sources for `signin_view_controller_delegate_views`,
add a `deps +=` entry for the producing mojo target:

```text
"//chrome/browser/ui/webui/signin/history_sync_optin:mojo_bindings",
```

The patch that adds this is the same
`chrome_browser_ui_desktop_deps_qnx.patch` that extends the desktop
`if` guard to include QNX; the new dep is the second hunk of that patch.

## Verification

Clean bootstrap (full reset of test tree + `gclient sync` + CEF copy +
`qnx_sync_sources.sh` + `cef_create_projects_qnx.sh`):

```text
qnx_sync_exit=0
bootstrap_exit=0
319 patches total (310 applied, 9 skipped, 0 failed)
```

Narrow build of the originally failing object (with the previously
generated mojo header removed to force regeneration):

```bash
rm -f out/qnx_release/gen/chrome/browser/ui/webui/signin/history_sync_optin/history_sync_optin.mojom.h
rm -f out/qnx_release/obj/chrome/browser/ui/ui/signin_view_controller_delegate_views.o
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release obj/chrome/browser/ui/ui/signin_view_controller_delegate_views.o
```

Result: `signin_view_exit=0`; the ninja log shows the mojo generator step
`.../history_sync_optin:mojo_bindings__generator` running before the
CXX step that consumes the header.

## Search hints

```bash
rg -n "history_sync_optin|signin_view_controller_delegate_views" docs/qnx/history/build-errors
```
