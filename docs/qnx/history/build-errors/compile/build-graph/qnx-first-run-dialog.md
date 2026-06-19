# QNX first_run_internal_posix ShowFirstRunDialog declaration

## Failure signature

Stage: compile
Category: build-graph
Target: `obj/chrome/browser/browser/first_run_internal_posix.o`

Primary diagnostic:

```text
../../chrome/browser/first_run/first_run_internal_posix.cc:89:3: error: use of undeclared identifier 'ShowFirstRunDialog'; did you mean 'ShouldShowFirstRunDialog'?
  ShowFirstRunDialog();
  ^~~~~~~~~~~~~~~~~~
  ShouldShowFirstRunDialog
```

## Root cause

QNX compiles `chrome/browser/first_run/first_run_internal_posix.cc`. That file includes `chrome/browser/first_run/first_run_dialog.h` and calls `ShowFirstRunDialog()`.

The first-run dialog declarations are guarded for Mac/Linux only:

```cpp
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

The views implementation (`chrome/browser/ui/views/first_run_dialog.cc`) is also added to `//chrome/browser/ui:ui` only in the Linux/Mac source block. QNX therefore sees the call site without the declaration, and a declaration-only fix would risk a later link failure.

## Fix

Patch: `cef/patch/patches/qnx/chromium/first_run_dialog_qnx.patch`

Changes:

- Add `BUILDFLAG(IS_QNX)` to the declaration guard in:
  - `chrome/browser/first_run/first_run_dialog.h`
- Add a narrow QNX-only `chrome/browser/ui/BUILD.gn` source block for:
  - `views/first_run_dialog.cc`
  - `views/first_run_dialog.h`

Do not widen the whole `is_linux || is_mac` block because it also pulls password relaunch promo sources and deps that were not part of this failure.

## Verification

```text
./out/qnx_release/ninja_qnx.sh \
  obj/chrome/browser/browser/first_run_internal_posix.o \
  obj/chrome/browser/ui/ui/first_run_dialog.o
EXIT:0
```

Patch state check on the patched tree:

```text
patch -p0 --reverse --dry-run < cef/patch/patches/qnx/chromium/first_run_dialog_qnx.patch
checking file chrome/browser/first_run/first_run_dialog.h
checking file chrome/browser/ui/BUILD.gn
Hunk #1 succeeded at 3481 (offset 3 lines).
rc=0
```
