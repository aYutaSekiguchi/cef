# QNX inactive window mouse event controller implementation missing

## Failure signature

Stage: link
Category: build-graph / platform-guard
Target: `//cef:cefsimple` via `./libcef.so`

After `HistorySyncOptinUI` was fixed, the first unresolved symbols were:

```text
./libcef.so: undefined reference to `tabs::InactiveWindowMouseEventController::~InactiveWindowMouseEventController()'
./libcef.so: undefined reference to `tabs::ScopedAcceptMouseEventsWhileWindowInactive::~ScopedAcceptMouseEventsWhileWindowInactive()'
./libcef.so: undefined reference to `tabs::InactiveWindowMouseEventController::AcceptMouseEventsWhileWindowInactive()'
```

## Root cause

QNX reaches desktop tab interaction code that references `InactiveWindowMouseEventController`, but `chrome/browser/ui/tabs/BUILD.gn` only compiled `inactive_window_mouse_event_controller.cc` for:

```gn
if (is_win || is_mac || is_linux || is_chromeos) { ... }
```

With `is_qnx=true` and `is_linux=false`, the declarations were visible through headers but the implementation object was not linked into `libcef.so`.

## Fix

Patch: `cef/patch/patches/qnx/chromium/inactive_window_mouse_event_controller_qnx.patch`

Add QNX to the source-selection guard:

```gn
if (is_win || is_mac || is_linux || is_chromeos || is_qnx) {
  sources += [ "inactive_window_mouse_event_controller.cc" ]
}
```

## Verification

After GN regeneration, `out/qnx_release/obj/cef/libcef.ninja` contains:

```text
inactive_window_mouse_event_controller.o
```

## Search hints

```bash
rg -n "InactiveWindowMouseEventController|ScopedAcceptMouseEventsWhileWindowInactive|inactive_window_mouse_event_controller_qnx" docs/qnx/history/build-errors
```
