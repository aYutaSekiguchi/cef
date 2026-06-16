# QNX: chrome_content_browser_client print preview include guard

## Stage

- stage: compile
- category: build-graph
- file: `chrome/browser/chrome_content_browser_client.cc`

## Failure signature

```text
In file included from ../../chrome/browser/chrome_content_browser_client.cc:534:
../../chrome/browser/printing/print_preview_dialog_controller.h:14:10: fatal error: 'components/printing/common/print.mojom.h' file not found
   14 | #include "components/printing/common/print.mojom.h"
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## Root cause

QNX disables printing and print preview in `tools/cef_create_projects_qnx.sh`:

```gn
enable_print_preview = false
enable_printing = false
```

`chrome_content_browser_client.cc` already guards the only `PrintPreviewDialogController` usage with `BUILDFLAG(ENABLE_PRINT_PREVIEW)`, but the header include was guarded only by `!BUILDFLAG(IS_ANDROID)`. That pulled in printing mojom generated headers even when print preview is disabled, and those headers are not generated in the QNX build graph.

## Fix

Match the include guard to the existing usage guard:

```cpp
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
#include "chrome/browser/printing/print_preview_dialog_controller.h"
#endif
```

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/browser/chrome_content_browser_client.o
```

Result: `chrome_content_browser_client.o` compiles successfully.

## Search hints

```bash
rg -n "print_preview_dialog_controller|print.mojom.h|ENABLE_PRINT_PREVIEW|chrome_content_browser_client" docs/qnx/history/build-errors/compile
```
