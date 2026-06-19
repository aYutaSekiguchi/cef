# QNX chrome_browser_interface_binders_webui_parts_desktop WebUI binder includes and updater mojo dep

## Failure signature

Stage: compile
Category: build-graph
Target: `obj/chrome/browser/browser/chrome_browser_interface_binders_webui_parts_desktop.o`

Primary diagnostics:

```text
chrome/browser/chrome_browser_interface_binders_webui_parts_desktop.cc: error: use of undeclared identifier 'ProfileCustomizationUI'
chrome/browser/chrome_browser_interface_binders_webui_parts_desktop.cc: error: use of undeclared identifier 'ProfilePickerUI'
```

After adding the includes, a clean build can also expose:

```text
fatal error: 'chrome/browser/ui/webui/updater/updater_ui.mojom.h' file not found
```

## Root cause

`chrome_browser_interface_binders_webui_parts_desktop.cc` is compiled on QNX and the binder registration code instantiates templates using `ProfileCustomizationUI` and `ProfilePickerUI` under `!IS_CHROMEOS` guards.

The include block that provides those WebUI controller declarations was guarded only for Win/Mac/Linux. QNX therefore compiled the use sites without the declarations.

The same include block also pulls `updater_ui.mojom.h`. QNX uses the updater WebUI code path, but `//chrome/browser:browser` did not explicitly depend on `//chrome/browser/ui/webui/updater:mojo_bindings` for QNX, so a clean build could compile before the generated mojo header existed.

## Fix

Patch: `cef/patch/patches/qnx/chromium/chrome_browser_interface_binders_webui_parts_desktop_qnx.patch`

Changes:

- Add `BUILDFLAG(IS_QNX)` to the desktop WebUI include guard in:
  - `chrome/browser/chrome_browser_interface_binders_webui_parts_desktop.cc`
- Add a QNX-only dependency in `chrome/browser/BUILD.gn`:
  - `//chrome/browser/ui/webui/updater:mojo_bindings`

Keep the dependency QNX-only; do not widen broad desktop dependency blocks unless all pulled targets are QNX-safe.

## Verification

Focused verification before recording the patch:

```text
ninja -C out/qnx_release obj/chrome/browser/browser/chrome_browser_interface_binders_webui_parts_desktop.o
EXIT:0
```

Patch format verification:

```text
cd /home/yuta/chromium/src
patch -p0 --dry-run < cef/patch/patches/qnx/chromium/chrome_browser_interface_binders_webui_parts_desktop_qnx.patch
checking file chrome/browser/BUILD.gn
checking file chrome/browser/chrome_browser_interface_binders_webui_parts_desktop.cc
rc=0
```

Note: when testing this patch with `git apply`, pass `-p0`; CEF-managed QNX patches use no `a/`/`b/` prefix paths and are applied with `-p0` first by the patcher.
