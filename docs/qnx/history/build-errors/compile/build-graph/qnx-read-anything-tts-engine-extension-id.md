# QNX: Read Anything TTS engine extension id

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/ui/ui/read_anything_untrusted_page_handler.o`

## Failure signature

```text
../../chrome/browser/ui/webui/side_panel/read_anything/read_anything_untrusted_page_handler.cc:623:23: error: no member named 'kComponentUpdaterTTSEngineExtensionId' in namespace 'extension_misc'
  623 |       extension_misc::kComponentUpdaterTTSEngineExtensionId;
      |                       ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
../../chrome/browser/ui/webui/side_panel/read_anything/read_anything_untrusted_page_handler.cc:1447:27: error: no member named 'kComponentUpdaterTTSEngineExtensionId' in namespace 'extension_misc'
```

## Root cause

`read_anything_untrusted_page_handler.cc` builds the non-ChromeOS Read Anything
TTS component path on QNX. That path references
`extension_misc::kComponentUpdaterTTSEngineExtensionId`.

The TTS engine extension ids in `chrome/common/extensions/extension_constants.h`
were only exposed for Windows, Linux, and macOS:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
```

QNX was therefore compiling the consumer without the declaration.

## Fix

Extend the TTS engine extension id guard, and the built-in first-party extension
id set guard, to include `BUILDFLAG(IS_QNX)`.

## Verification

```bash
cd /home/yuta/chromium/test/src
ninja -C out/qnx_release \
  obj/chrome/browser/ui/ui/read_anything_untrusted_page_handler.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "kComponentUpdaterTTSEngineExtensionId|read_anything_untrusted_page_handler|TTSEngineExtensionId" docs/qnx/history/build-errors/compile
```
