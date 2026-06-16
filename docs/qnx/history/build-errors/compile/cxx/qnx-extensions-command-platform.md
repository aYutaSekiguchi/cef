# QNX: extensions CommandPlatform Linux branch

## Stage

- stage: compile
- category: cxx / platform-guard
- target: `//extensions/common:common`
- file: `extensions/common/command.cc`

## Failure signature

```text
FAILED: obj/extensions/common/common/command.o
../../extensions/common/command.cc:126:2: error: Unsupported platform
  126 | #error Unsupported platform
      |  ^
1 error generated.
```

## Root cause

`Command::CommandPlatform()` enumerated Win, Mac, ChromeOS, Linux, and Desktop Android, then hit the final `#error`. QNX is POSIX but extensions treats it as a non-Linux desktop, so it fell through to the unsupported branch.

## Fix

Patch: `qnx/chromium/extensions_command_qnx_linux_fallback`

Extend the existing Linux branch to also cover QNX, reusing the Linux keybinding platform identifier:

```cpp
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)
  return ui::kKeybindingPlatformLinux;
```

`IS_DESKTOP_ANDROID` already shares the same Linux keybinding identifier in this function, so this matches the existing precedent.

## Verification

Narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/extensions/common/common/command.o
```

Result:

```text
[7654/7654] CXX obj/extensions/common/common/command.o
```

## Search hints

```bash
rg -n "Unsupported platform|extensions/common/command|CommandPlatform" /tmp/*.log docs/qnx/history/build-errors
```
