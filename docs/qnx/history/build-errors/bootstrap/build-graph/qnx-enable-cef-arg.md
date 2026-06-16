# QNX: enable CEF arg in args.gn template

## Stage

- stage: bootstrap
- category: build-graph / args-gn
- file: `tools/cef_create_projects_qnx.sh`

## Failure signature

```text
../../chrome/browser/ui/webui/about/about_ui.cc:89:10: fatal error: 'cef/grit/cef_resources.h' file not found
   89 | #include "cef/grit/cef_resources.h"
```

The same `gen/cef/grit/` directory was empty even after `gn gen`, because the grit action was not selected into the build graph.

## Root cause

`gn_config.patch` adds `if (enable_cef) { deps += [ "//cef:cef_resources" ] }` style guards to the Chrome pak / repack templates and to `chrome_browser_browser.patch`, but the Phase 4 template in `tools/cef_create_projects_qnx.sh` did not set `enable_cef = true`. The default `enable_cef = false` left `cef:cef_resources` out of the QNX build graph, so `cef_resources.h` and `cef_resources.pak` were never generated, and `chrome/browser/ui/webui/about/about_ui.cc` failed to find the include.

## Fix

Add `enable_cef = true` to the CEF specific section in the `args.gn` heredoc emitted by `cef_create_projects_qnx.sh`:

```gn
# CEF specific
enable_cef = true
cef_target_arch = "x64"
cef_use_alloc_shim = false
```

## Verification

After `gn gen` and the one-time grit action:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . gen/cef/grit/cef_resources.h
# -> gen/cef/grit/cef_resources.h is generated
ninja -C . obj/chrome/browser/ui/webui/about/impl/about_ui.o
# -> compiles successfully
```

## Search hints

```bash
rg -n "cef/grit/cef_resources.h|enable_cef|gen/cef/grit" /tmp/*.log docs/qnx/history/build-errors
```
