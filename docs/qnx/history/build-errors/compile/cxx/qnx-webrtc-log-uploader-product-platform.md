# QNX: WebRTC log uploader product platform branch

## Stage

- stage: compile
- category: cxx / platform-guard
- file: `chrome/browser/media/webrtc/webrtc_log_uploader.cc`

## Failure signature

```text
../../chrome/browser/media/webrtc/webrtc_log_uploader.cc:109:2: error: Platform not supported.
  109 | #error Platform not supported.
      |  ^
../../chrome/browser/media/webrtc/webrtc_log_uploader.cc:112:26: error: use of undeclared identifier 'product'
  112 |     return base::StrCat({product, "_webrtc"});
      |                          ^~~~~~~
../../chrome/browser/media/webrtc/webrtc_log_uploader.cc:114:10: error: use of undeclared identifier 'product'
  114 |   return product;
      |          ^~~~~~~
```

## Root cause

`GetLogUploadProduct()` enumerates desktop/mobile platforms and falls into `#error Platform not supported` for QNX. The function only chooses a product string for WebRTC log upload; QNX can safely reuse the existing Linux string in the same way other QNX CEF patches reuse Linux-like desktop behavior.

## Fix

Extend the Linux branch to QNX:

```cpp
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)
```

This keeps the product string as `Chrome_Linux` / `Chrome_Linux_ASan` and avoids inventing a new upload product policy.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/media/webrtc/webrtc/webrtc_log_uploader.o
```

Result: `webrtc_log_uploader.o` compiles successfully.

## Search hints

```bash
rg -n "webrtc_log_uploader|Platform not supported|Chrome_Linux|GetLogUploadProduct" docs/qnx/history/build-errors/compile
```
