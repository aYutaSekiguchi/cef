# QNX: Blink NavigatorBase reduced platform branch

## Stage

- stage: compile
- category: cxx / platform-guard
- target: `//third_party/blink/renderer/core:core`
- file: `third_party/blink/renderer/core/execution_context/navigator_base.cc`

## Failure signature

```text
FAILED: obj/third_party/blink/renderer/core/core/navigator_base.o
../../third_party/blink/renderer/core/execution_context/navigator_base.cc:40:2: error: Unsupported platform
   40 | #error Unsupported platform
      |  ^
1 error generated.
```

## Root cause

`GetReducedNavigatorPlatform()` enumerated Android, Mac, Win, Fuchsia, Linux/ChromeOS, and iOS, then hit the final `#error`. QNX is POSIX but Blink treats it as a non-Linux desktop, so it fell through to the unsupported branch.

## Fix

Patch: `qnx/chromium/blink_navigator_base_qnx_linux_fallback`

Extend the Linux/ChromeOS branch to also cover QNX and reuse the existing `"Linux x86_64"` reduced navigator platform string:

```cpp
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_QNX)
  return "Linux x86_64";
```

This matches the desktop-class treatment used elsewhere in the QNX port.

## Verification

Narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/third_party/blink/renderer/core/core/navigator_base.o
```

Result:

```text
[5031/5031] CXX obj/third_party/blink/renderer/core/core/navigator_base.o
```

## Search hints

```bash
rg -n "Unsupported platform|navigator_base|GetReducedNavigatorPlatform" /tmp/*.log docs/qnx/history/build-errors
```
