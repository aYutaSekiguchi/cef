# QNX: GlobalFeatures whats_new_registry on QNX

## Stage

- stage: compile
- category: cxx / undeclared-identifier
- target: `obj/chrome/browser/ui/ui/user_education_internals_ui.o`

## Failure signature

```text
../../chrome/browser/ui/webui/user_education_internals/user_education_internals_ui.cc:37:54: error: no member named 'whats_new_registry' in 'GlobalFeatures'
   37 |   auto* registry = g_browser_process->GetFeatures()->whats_new_registry();
```

## Root cause

`user_education_internals_ui.cc` calls
`g_browser_process->GetFeatures()->whats_new_registry()` under a
`!BUILDFLAG(IS_CHROMEOS)` guard, so QNX takes that branch.

The accessor, the `whats_new_registry_` member, the
`CreateWhatsNewRegistry()` factory declaration and definition, and the
`#include` of the default-browser / whats-new headers in
`chrome/browser/global_features.{h,cc}` are all gated by:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

QNX is not in that set, so `whats_new_registry()` is not declared on
QNX.

`chrome/browser/BUILD.gn` also gates the
`//chrome/browser/ui/webui/whats_new:mojo_bindings` public_dep entry on
`is_win || is_mac || is_linux`, missing QNX.

## Fix

Extend every relevant `IS_WIN || IS_MAC || IS_LINUX` guard in
`chrome/browser/global_features.h` (4 spots), `global_features.cc`
(4 spots), and the `is_win || is_mac || is_linux` public_deps block in
`chrome/browser/BUILD.gn` to also include `IS_QNX`. The forward-decl
block at h:22 covers both `whats_new::WhatsNewRegistry` and
`default_browser::DefaultBrowserManager`; both feature targets already
allow QNX (`//chrome/browser/default_browser` and
`//chrome/browser/ui/startup/default_browser_prompt`).

## Verification

Clean bootstrap (full reset of test tree + `gclient sync` + CEF copy +
`qnx_sync_sources.sh` + `cef_create_projects_qnx.sh`):

```bash
./tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root $QNX_SDP
```

Narrow build of the originally failing object:

```bash
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release obj/chrome/browser/ui/ui/user_education_internals_ui.o
```

Result: `EXIT:0`; no errors emitted.

## Search hints

```bash
rg -n "whats_new_registry|GlobalFeatures" docs/qnx/history/build-errors
```
