# QNX cefsimple missing entrypoint and X11-only Linux sample sources

## Failure signatures

Stage: compile/link
Category: build-graph / platform-guard
Targets: `//cef:cefsimple`, `//cef:cefsimple_capi`

Initial link failure from `/tmp/cefsimple_build.log`:

```text
FAILED: ... "./cefsimple" LINK ./cefsimple
... qcc ... -o "./cefsimple" -Wl,--start-group @"./cefsimple.rsp" ./libcef.so ... -Wl,--end-group
.../crt1.S:88:(.text+0xc8): undefined reference to `main'
```

`cefsimple.rsp` only contained common sources and did not contain any file defining `main()`.

A first attempt to reuse Linux sample sources on QNX advanced to a compile failure:

```text
[77338/77442] CXX obj/cef/cefsimple/cefsimple_linux.o
../../cef/tests/cefsimple/cefsimple_linux.cc:8:10: fatal error: 'X11/Xlib.h' file not found
#include <X11/Xlib.h>
         ^~~~~~~~~~~~
```

## Root cause

CEF's sample executable targets add platform entrypoint sources only under `if (is_linux)`. QNX has `is_qnx = true` and `is_linux = false`, so the common source list was used without any file defining `main()`.

However, directly widening the Linux branch to `is_linux || is_qnx` is not correct: the Linux sample files are X11-oriented and can include `<X11/Xlib.h>`/`<X11/Xatom.h>` when `CEF_X11` is defined by CEF's Linux platform headers. QNX does not support X11 in this port.

## Fix

Files:

- `cef/BUILD.gn`
- `cef/patch/qnx/chromium/new_files/cef/tests/cefsimple/cefsimple_qnx.cc`
- `cef/patch/qnx/chromium/new_files/cef/tests/cefsimple/simple_handler_qnx.cc`
- `cef/patch/qnx/chromium/new_files/cef/tests/cefsimple_capi/cefsimple_qnx.c`
- `cef/patch/qnx/chromium/new_files/cef/tests/cefsimple_capi/simple_handler_qnx.c`

Keep Linux sources Linux-only, and add QNX-specific sample entrypoints/handlers:

```gn
if (is_linux) {
  sources += includes_linux +
             gypi_paths2.cefsimple_sources_linux
  ...
}

if (is_qnx) {
  sources += [
    "tests/cefsimple/cefsimple_qnx.cc",
    "tests/cefsimple/simple_handler_qnx.cc",
  ]
}
```

The QNX files are minimal POSIX-style CEF sample entrypoints with no X11 includes. The QNX `PlatformTitleChange` implementation is a no-op.

## Verification

After regenerating GN, `gn desc` includes QNX files and excludes Linux/X11 files:

```text
//cef/tests/cefsimple/simple_app.cc
//cef/tests/cefsimple/simple_handler.cc
//cef/tests/cefsimple/cefsimple_qnx.cc
//cef/tests/cefsimple/simple_handler_qnx.cc
```

Command used:

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
buildtools/linux64/gn desc out/qnx_release //cef:cefsimple sources
```

A wider object build was started; it did not reach these QNX sample objects before the local timeout due to a broad rebuild, and no compile diagnostics appeared before timeout.

## Search hints

```bash
rg -n "undefined reference to `main|cefsimple_linux|cefsimple_qnx|X11/Xlib.h|cefsimple_sources_linux|cefsimple_capi_sources_linux" docs/qnx/history/build-errors
```
