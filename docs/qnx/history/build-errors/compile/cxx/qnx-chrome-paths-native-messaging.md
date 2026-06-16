# QNX: Chrome native messaging path constants

## Stage

- stage: compile
- category: cxx / platform-guard
- target: `//chrome/browser/extensions:extensions`
- files:
  - `chrome/common/chrome_paths.h`
  - `chrome/common/chrome_paths.cc`
  - `chrome/common/BUILD.gn`

## Failure signature

```text
../../chrome/browser/extensions/api/messaging/launch_context_posix.cc:45:40: error: no member named 'DIR_USER_NATIVE_MESSAGING' in namespace 'chrome'
   45 |     result = FindManifestInDir(chrome::DIR_USER_NATIVE_MESSAGING, host_name);
../../chrome/browser/extensions/api/messaging/launch_context_posix.cc:48:40: error: no member named 'DIR_NATIVE_MESSAGING' in namespace 'chrome'
   48 |     result = FindManifestInDir(chrome::DIR_NATIVE_MESSAGING, host_name);
```

## Root cause

`chrome/common/chrome_paths.h` only declared `DIR_NATIVE_MESSAGING` and `DIR_USER_NATIVE_MESSAGING` under:

```cpp
#if BUILDFLAG(ENABLE_EXTENSIONS_CORE) && \
    (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_ANDROID))
```

QNX enables `ENABLE_EXTENSIONS_CORE=1` and `launch_context_posix.cc` is included via `is_posix`, so the path constants were referenced but never declared.

The corresponding switch cases in `chrome/common/chrome_paths.cc` and the `chrome_paths_linux.cc` source in `chrome/common/BUILD.gn` were also restricted to Linux/ChromeOS, so the path provider entries had no source to back them up.

## Fix

Patch: `qnx/chromium/chrome_paths_native_messaging_qnx`

- Extend the `chrome_paths.h` guard to include `BUILDFLAG(IS_QNX)`.
- Extend the matching `chrome_paths.cc` guard.
- Add `is_qnx` to the `chrome/common/BUILD.gn` condition that pulls in `chrome_paths_linux.cc`, which provides the XDG directory provider that backs the new path entries on QNX.

`launch_context_posix.cc` itself needs no edit; the `IS_LINUX` guard around `allow_new_privs` is separate.

## Verification

Narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
gn gen .
ninja -C . obj/chrome/browser/extensions/extensions/launch_context_posix.o
```

Result:

```text
[1659/1659] CXX obj/chrome/browser/extensions/extensions/launch_context_posix.o
```

The first build run after `gn gen` had to replay many DevTools and chrome_paths_linux actions because the QNX build did not previously compile chrome_paths_linux.cc, but no failures were observed.

## Search hints

```bash
rg -n "DIR_USER_NATIVE_MESSAGING|DIR_NATIVE_MESSAGING|launch_context_posix|chrome_paths_linux" /tmp/*.log docs/qnx/history/build-errors
```
