# QNX: Settings UI compose include guard

## Stage

- stage: compile
- category: build-graph / generated-header
- target: `obj/chrome/browser/ui/ui/settings_ui.o`

## Failure signature

```text
In file included from ../../chrome/browser/ui/webui/settings/settings_ui.cc:24:
../../chrome/browser/compose/compose_enabling.h:13:10: fatal error: 'chrome/browser/compose/proto/compose_optimization_guide.pb.h' file not found
   13 | #include "chrome/browser/compose/proto/compose_optimization_guide.pb.h"
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## Root cause

QNX has `enable_compose=false` because `components/compose/features.gni` enables
Compose only on macOS, Windows, Linux, and ChromeOS:

```gn
enable_compose = is_mac || is_win || is_linux || is_chromeos
```

`settings_ui.cc` already guards `ComposeEnabling` usage with
`BUILDFLAG(ENABLE_COMPOSE)`, but it included `compose_enabling.h`
unconditionally. That header includes the generated Compose optimization-guide
proto header, which is not generated when `enable_compose=false`.

## Fix

Include `components/compose/buildflags.h` and guard `compose_enabling.h` with
`BUILDFLAG(ENABLE_COMPOSE)`, matching the existing use-site guard.

## Verification

```bash
cd /home/yuta/chromium/test/src
ninja -C out/qnx_release obj/chrome/browser/ui/ui/settings_ui.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "compose_optimization_guide.pb.h|compose_enabling|settings_ui|ENABLE_COMPOSE" docs/qnx/history/build-errors/compile
```
