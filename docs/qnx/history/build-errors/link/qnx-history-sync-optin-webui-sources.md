# QNX HistorySyncOptinUI WebUI implementation missing from libcef

## Failure signature

Stage: link
Category: build-graph / platform-guard
Target: `//cef:cefsimple` via `./libcef.so`

After feature engagement constants were fixed, the first remaining unresolved symbols were:

```text
./libcef.so: undefined reference to `HistorySyncOptinUI::AppendHistorySyncOptinQueryParams(GURL const&, HistorySyncOptinLaunchContext)'
./libcef.so: undefined reference to `HistorySyncOptinUI::kWebUIControllerType'
./libcef.so: undefined reference to `HistorySyncOptinUI::Initialize(Browser*, std::optional<bool>, ...)'
```

## Root cause

QNX desktop profile/signin UI sources reference `HistorySyncOptinUI`, and an earlier QNX fix added the `history_sync_optin:mojo_bindings` dependency needed for generated headers.

However, `chrome/browser/ui/webui/signin/BUILD.gn` still compiled the actual `HistorySyncOptinUI` declaration/implementation sources only for:

```gn
if (is_win || is_mac || is_linux) { ... }
```

With `is_qnx=true` and `is_linux=false`, QNX could compile call sites but did not link the WebUI implementation objects into `libcef.so`.

## Fix

Patch: `cef/patch/patches/qnx/chromium/history_sync_optin_webui_sources_qnx.patch`

Change both desktop guards in `chrome/browser/ui/webui/signin/BUILD.gn` to include QNX:

```gn
if (is_win || is_mac || is_linux || is_qnx) {
  sources += [
    "history_sync_optin/history_sync_optin_handler.h",
    "history_sync_optin/history_sync_optin_ui.h",
  ]
  ...
}

if (is_win || is_mac || is_linux || is_qnx) {
  sources += [
    "history_sync_optin/history_sync_optin_handler.cc",
    "history_sync_optin/history_sync_optin_ui.cc",
  ]
  ...
}
```

## Verification

After GN regeneration, `libcef.ninja` contains the implementation objects:

```text
history_sync_optin_handler.o
history_sync_optin_ui.o
```

Command used:

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
buildtools/linux64/gn --root=. -q --regeneration gen out/qnx_release
```

Then checked `out/qnx_release/obj/cef/libcef.ninja` for `history_sync_optin_ui.o`.

## Search hints

```bash
rg -n "HistorySyncOptinUI::kWebUIControllerType|AppendHistorySyncOptinQueryParams|history_sync_optin_webui_sources_qnx|history_sync_optin_ui.o" docs/qnx/history/build-errors
```
