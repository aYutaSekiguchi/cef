# QNX: Read Anything Wasm TTS feature helper

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/ui/read_anything/read_anything/read_anything_service.o`

## Failure signature

```text
../../chrome/browser/ui/read_anything/read_anything_service.cc:106:18: error: no member named 'IsWasmTtsEngineAutoInstallDisabled' in namespace 'features'
  106 |   if (!features::IsWasmTtsEngineAutoInstallDisabled()) {
      |                  ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## Root cause

`read_anything_service.cc` builds `SetupDesktopEngine()` for all non-ChromeOS
platforms. QNX is non-ChromeOS, so it calls
`features::IsWasmTtsEngineAutoInstallDisabled()`.

The accessibility feature and helper in `ui/accessibility/accessibility_features.{h,cc}`
were only declared/defined for Windows, macOS, and Linux:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

QNX therefore compiled the Read Anything consumer without the feature helper
declaration.

## Fix

Extend the `kWasmTtsEngineAutoInstallDisabled` and
`IsWasmTtsEngineAutoInstallDisabled()` guard to include `BUILDFLAG(IS_QNX)`.

## Verification

```bash
cd /home/yuta/chromium/test/src
ninja -C out/qnx_release \
  obj/chrome/browser/ui/read_anything/read_anything/read_anything_service.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "IsWasmTtsEngineAutoInstallDisabled|kWasmTtsEngineAutoInstallDisabled|read_anything_service" docs/qnx/history/build-errors/compile
```
