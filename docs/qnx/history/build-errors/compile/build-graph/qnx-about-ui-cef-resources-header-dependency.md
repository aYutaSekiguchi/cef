# QNX: about_ui missing cef_resources generated header dependency

## Stage

- stage: compile
- category: build-graph
- target: `//chrome/browser/ui/webui/about:impl`
- file: `chrome/browser/ui/webui/about/about_ui.cc`

## Failure signature

```text
../../chrome/browser/ui/webui/about/about_ui.cc:89:10: fatal error: 'cef/grit/cef_resources.h' file not found
   89 | #include "cef/grit/cef_resources.h"
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## Root cause

`about_ui.cc` includes `cef/grit/cef_resources.h` under `BUILDFLAG(ENABLE_CEF)`. CEF's older `chrome_browser_browser.patch` adds `//cef:cef_resources` to `//chrome/browser/ui`, but Chromium 147 split `chrome/browser/ui/webui/about` into a modular `source_set("impl")`. That target compiles `about_ui.cc` directly and does not inherit the generated-header dependency, so Ninja can compile `about_ui.o` before `gen/cef/grit/cef_resources.h` exists.

This is separate from `enable_cef = true`: even with `enable_cef` set and `//cef:cef_resources` present in the graph, `about:impl` still needs a direct dependency edge.

## Fix

Add a QNX CEF-managed patch for `chrome/browser/ui/webui/about/BUILD.gn`:

```gn
import("//cef/libcef/features/features.gni")
...
if (enable_cef) {
  deps += [ "//cef:cef_resources" ]
}
```

The patch assumes `chrome_browser_ui_asserts_allow_qnx.patch` has already added `is_qnx` to the top-level platform assert, so register it after that patch in `patch/patch.cfg`.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
gn gen .
ninja -C . obj/chrome/browser/ui/webui/about/impl/about_ui.o
```

Result: `about_ui.o` compiles and `gen/cef/grit/cef_resources.h` is generated first.

## Search hints

```bash
rg -n "cef/grit/cef_resources.h|about_ui.cc|about:impl|cef_resources" docs/qnx/history/build-errors/compile
```
