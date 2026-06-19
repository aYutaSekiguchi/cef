# QNX content TtsPlatformImpl singleton missing

## Failure signature

Stage: link / compile
Category: platform-api-gap / build-graph
Target: `//cef:cefsimple` via `./libcef.so`

After the inactive window mouse controller fix, the first unresolved symbol was:

```text
./libcef.so: undefined reference to `content::TtsPlatformImpl::GetInstance()'
```

A first minimal QNX singleton then failed to compile because `TtsPlatformImpl` still has pure virtual methods inherited from `TtsPlatform`:

```text
unimplemented pure virtual method 'PlatformImplSupported' in 'TtsPlatformImplQnx'
unimplemented pure virtual method 'PlatformImplInitialized' in 'TtsPlatformImplQnx'
unimplemented pure virtual method 'Speak' in 'TtsPlatformImplQnx'
unimplemented pure virtual method 'StopSpeaking' in 'TtsPlatformImplQnx'
unimplemented pure virtual method 'IsSpeaking' in 'TtsPlatformImplQnx'
```

## Root cause

`content/browser/speech/tts_platform_impl.cc` provides common no-op method implementations, but each platform must provide the static singleton:

```cpp
TtsPlatformImpl* TtsPlatformImpl::GetInstance();
```

Linux provides this in `tts_linux.cc`, but that implementation depends on `//third_party/speech-dispatcher` and `//ui/linux:linux_ui`, which are not valid QNX dependencies. QNX therefore needs a small no-op backend instead of reusing the Linux source.

## Fix

Files:

- New file: `cef/patch/qnx/chromium/new_files/content/browser/speech/tts_qnx.cc`
- Patch: `cef/patch/patches/qnx/chromium/content_tts_qnx.patch`

`tts_qnx.cc` defines a `base::NoDestructor` singleton deriving from `TtsPlatformImpl`. It implements the required pure virtual `TtsPlatform` methods as unsupported/no-op and relies on the common error/shutdown/voice-ordering no-op method implementations from `tts_platform_impl.cc`.

`content/browser/BUILD.gn` now includes the QNX source:

```gn
if (is_qnx) {
  sources += [ "speech/tts_qnx.cc" ]
}
```

## Verification

After GN regeneration, `out/qnx_release/obj/cef/libcef.ninja` contains:

```text
tts_qnx.o
```

and does not contain `tts_linux.o`.

A targeted `ninja -C out/qnx_release obj/content/browser/browser/tts_qnx.o` was rerun after adding the pure virtual method implementations. It produced no diagnostics before the local timeout while completing prerequisite generated targets.

## Search hints

```bash
rg -n "TtsPlatformImpl::GetInstance|tts_qnx|content_tts_qnx|speech-dispatcher" docs/qnx/history/build-errors
```
