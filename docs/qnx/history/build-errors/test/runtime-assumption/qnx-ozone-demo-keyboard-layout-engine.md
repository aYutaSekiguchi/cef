# QNX ozone_demo startup null keyboard layout engine

- Date: 2026-07-03
- Signature: `ozone_demo --ozone-platform=qnx` signal 11 in `std::__pad_and_output`, `si_addr=0x0`
- Stage: test
- Category: runtime-assumption
- Scope: QNX Ozone / `ui/ozone/demo:ozone_demo`

## Symptoms

`ozone_demo --ozone-platform=qnx --window-size=320x240` under QEMU virgl crashed immediately after QNX Ozone UI initialization:

```text
Received signal 11 si_addr=0x0
Symbol: ./ozone_demo _ZNSt3__216__pad_and_outputB7v160006IcNS_11char_traitsIcEEEENS_19ostreambuf_iteratorIT_T0_EES6_PKS4_S8_S8_RNS_8ios_baseES4_ + 0x0000000002ea
segmentation violation (core dumped)
__PI_QNX_EXIT__:139
```

`ozone_demo --help` exited 0, and `--ozone-platform=headless` did not segfault before timeout.

## Root cause

`ui/ozone/demo/ozone_demo.cc` calls:

```cpp
ui::KeyboardLayoutEngineManager::GetKeyboardLayoutEngine()
    ->SetCurrentLayoutByName("us", base::DoNothing());
```

Headless, Wayland, and X11 Ozone platforms install a `StubKeyboardLayoutEngine` during `InitializeUI()`. The QNX Ozone platform did not, so the demo dereferenced a null layout engine after `OzonePlatformQnxImpl::InitializeUI()` returned successfully.

## Fix pattern

Every Ozone platform that can run the demo/test startup path must install a keyboard layout engine during UI initialization, even if the platform has only stubbed input translation initially:

```cpp
keyboard_layout_engine_ = std::make_unique<StubKeyboardLayoutEngine>();
KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(
    keyboard_layout_engine_.get());
```

## Applied change

QNX Ozone `InitializeUI()` now creates a `StubKeyboardLayoutEngine` and registers it with `KeyboardLayoutEngineManager`. The same fix shape matches the existing headless/Wayland/X11 platforms.

## Verification

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_runtime_fix ui/ozone/demo:ozone_demo -k 20
```

Result:

```text
[842/842] LINK ./ozone_demo
```

Runtime after the fix no longer exits 139. It reaches the next renderer-surface blocker and times out because `ozone_demo` keeps running:

```text
[WARNING:ui/ozone/demo/window_manager.cc:45] No display delegate; falling back to test window
[ERROR:ui/ozone/demo/software_renderer.cc:44] Failed to create software surface
[ERROR:ui/ozone/demo/demo_window.cc:100] Failed to initialize renderer.
TimeoutError: QNX command timed out after 45.0 seconds
```

## Files touched

- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc`

The same debugging pass also made QNX Screen error logging null-safe in:

- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_screen_context.cc`
- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_platform_event_source.cc`

## Related notes

- `docs/qnx/history/research/qnx-ozone-phase5-runtime-startup-crash-fix-2026-07-03.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ceftests-startup-crashes.md`
