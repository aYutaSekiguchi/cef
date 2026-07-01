# QNX ceftests build graph and platform shims

## Failure signatures

Stage: compile/link/test enablement
Category: build-graph / platform-guard
Target: `//cef:ceftests`

While enabling CEF API tests on QNX, the Linux-only `ceftests` target wiring left QNX without resource copies, QNX resource lookup, and a registered `qnx_run_test.sh --ceftests` module. Once enabled, QNX hit platform guard/link blockers:

```text
../../cef/tests/ceftests/os_rendering_unittest.cc:831:19: error: use of undeclared identifier 'kExpandedSelectRect'
../../cef/tests/ceftests/osr_accessibility_unittest.cc:225:2: error: "Unsupported platform"
undefined reference to `client::MainMessageLoopExternalPump::Create()'
./libcef.so: hidden symbol `CefPrintSettings::Create()' in .../print_settings_ctocpp.o is referenced by DSO
```

## Root cause

`ceftests` was Linux-only for platform resource glue and copied resources. QNX can reuse the common CEF API test sources, but needs QNX-specific replacements for Linux/X11 or Linux `/proc/self/exe` assumptions:

- `/proc/self/exefile` instead of Linux `/proc/self/exe` for locating `ceftests_files`.
- No X11 `keysym.h`, while OSR tests still need POSIX-style key codes.
- A QNX external-message-pump factory symbol so `run_all_unittests.cc` links.
- Printing is disabled on QNX, but generated CEF C API glue still references `CefPrintSettings::Create()`; provide an in-memory QNX implementation for API validation without enabling Chromium printing services.

## Fix

Files:

- `cef/BUILD.gn`
- `cef/cef_paths2.gypi`
- `cef/tests/ceftests/os_rendering_unittest.cc`
- `cef/tests/ceftests/osr_accessibility_unittest.cc`
- `cef/tools/qnx_run_test.sh`
- `cef/tools/qnx_tests/modules/__init__.py`
- `cef/tools/qnx_tests/modules/ceftests.py` (tracked in the CEF repo; do not
  mirror it under `patch/qnx/chromium/new_files/cef/tools/...` because the
  bootstrap Phase 1 copy would overwrite the tracked file and dirty the CEF
  worktree)
- `cef/patch/qnx/chromium/new_files/cef/tests/ceftests/resource_util_qnx.cc`
- `cef/patch/qnx/chromium/new_files/cef/tests/shared/browser/main_message_loop_external_pump_qnx.cc`
- `cef/patch/qnx/chromium/new_files/cef/libcef/browser/print_settings_impl_qnx.cc`

The QNX build now includes `ceftests`, copies `ceftests_files`, uses QNX resource lookup, and registers `tools/qnx_run_test.sh --ceftests`.

## Verification

```bash
# GN source selection includes QNX resource glue, not Linux resource glue.
export QNX_HOST=/home/yuta/qnx800/host/linux/x86_64
export QNX_TARGET=/home/yuta/qnx800/target/qnx
buildtools/linux64/gn desc out/qnx_release //cef:ceftests sources \
  | grep -E 'cef/tests/ceftests/resource_util_(qnx|linux)\.cc|cef/tests/shared/browser/resource_util_posix\.cc'

# Build succeeds.
./out/qnx_release/ninja_qnx.sh ceftests

# Runner registration.
python3 cef/tools/qnx_tests/cli.py --list | grep -- --ceftests
```

A narrow QEMU runtime attempt was blocked by missing host network setup:

```text
ERROR: tap0 not found. Run: sudo cef/tools/qnx_setup_env.sh
```

The local harness could not run `sudo cef/tools/qnx_setup_env.sh` because sudo requires an interactive password.

## Search hints

```bash
rg -n "ceftests|resource_util_qnx|MainMessageLoopExternalPumpQnx|PrintSettingsQnx|kExpandedSelectRect" docs/qnx/history/build-errors cef/BUILD.gn cef/tests/ceftests
```
