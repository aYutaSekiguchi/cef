# ui/events KeycodeConverter needs a QNX native keycode mapping

- Date: 2026-06-09
- Signature: `keycode_converter.cc:43:2: error: Unsupported platform`
- Stage: compile
- Category: feature-guard
- Scope: `ui/events/keycodes/dom/keycode_converter.cc`
- External references:
  - QNX 8 Screen input events: `SCREEN_EVENT_KEYBOARD`, `SCREEN_PROPERTY_SCAN`, `SCREEN_PROPERTY_SYM`, `SCREEN_PROPERTY_KEY_CAP`
  - QNX sysroot headers: `<screen/screen.h>`, `<sys/keycodes.h>`, `<sys/usbcodes.h>`

## Symptoms

After the PDFium QNX `fxge` platform implementation, `cefsimple` advanced to `ui/events`:

```text
FAILED: obj/ui/events/dom_keycode_converter/keycode_converter.o
../../ui/events/keycodes/dom/keycode_converter.cc:43:2: error: Unsupported platform
```

The many follow-on diagnostics from `dom_code_data.inc` were fallout. `DOM_CODE` is defined by a platform branch in `keycode_converter.cc`; after `#error Unsupported platform`, the include still parses without the macro definition.

## Root cause

`keycode_converter.cc` has native keycode table branches for Windows, Linux/ChromeOS, Apple, Android, and Fuchsia, but not QNX.

Linux/ChromeOS could not be reused directly:

- it includes `<linux/input.h>`;
- QNX SDP 8 sysroot does not provide `<linux/input.h>`;
- QNX build args currently have `use_xkbcommon = false`, `ozone_platform_x11 = false`, `ozone_platform_wayland = false`, and `ozone_platform_drm = false`;
- Linux helper functions depend on evdev/XKB concepts such as `KEY_RESERVED`, `KEY_MAX`, and `KEY_PLAYCD`.

## QNX keyboard event findings

QNX Screen keyboard events expose these relevant properties:

- `SCREEN_PROPERTY_FLAGS`
- `SCREEN_PROPERTY_MODIFIERS`
- `SCREEN_PROPERTY_KEY_CAP`
- `SCREEN_PROPERTY_SCAN`
- `SCREEN_PROPERTY_SYM`

QNX documentation describes `SCREEN_PROPERTY_SCAN` as the scan code / physical key position and points to `<sys/usbcodes.h>`. The sysroot confirms that `<sys/usbcodes.h>` defines USB keyboard/keypad usage IDs without the USB usage page prefix, for example:

```c
#define KS_a       0x04
#define KS_Enter   0x28
#define KS_Escape  0x29
#define KS_f1      0x3a
```

Chromium's `DomCode` table stores USB usage values with the usage page in the upper bits, for example:

```text
DomCode::US_A / KeyA: 0x070004
QNX SCREEN_PROPERTY_SCAN for A: 0x04
```

Therefore, the natural QNX native keycode mapping is:

```text
native_keycode = usb_keycode & 0xffff
```

but only for USB keyboard/keypad page `0x07`. Other usage pages are not represented by `<sys/usbcodes.h>` and should map to invalid native keycode `0`.

## Fix pattern

Add a QNX-specific helper and table branch:

```cc
#if BUILDFLAG(IS_QNX)
constexpr int UsbKeycodeToQnxScanCode(uint32_t usb_keycode) {
  constexpr uint32_t kUsbKeyboardKeypadUsagePage = 0x00070000u;
  return (usb_keycode & 0xffff0000u) == kUsbKeyboardKeypadUsagePage
             ? static_cast<int>(usb_keycode & 0xffffu)
             : 0;
}
#endif

#elif BUILDFLAG(IS_QNX)
#define DOM_CODE(usb, evdev, xkb, win, mac, code, id) \
  { usb, UsbKeycodeToQnxScanCode(usb), code }
```

The code comment intentionally records the QNX Screen / `<sys/usbcodes.h>` reasoning so a future QNX Screen/Ozone keyboard backend can revisit or extend the mapping in place.

## Verification

- Clean-tree reset plus bootstrap succeeded:
  - `git checkout -f`: `0`
  - `gclient sync -f -R`: `0`
  - `./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800`: `0`
- Post-bootstrap source markers confirm the managed patch is applied:
  - `UsbKeycodeToQnxScanCode` appears in `ui/events/keycodes/dom/keycode_converter.cc`
  - the code comment includes the `SCREEN_PROPERTY_SCAN` / `<sys/usbcodes.h>` rationale
  - a QNX `DOM_CODE` branch feeds `UsbKeycodeToQnxScanCode(usb)`
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` removed the target signatures:
  - `keycode_converter.cc: Unsupported platform` hits: `0`
  - `dom_code_data.inc: DOM_CODE` fallout hits: `0`
- The next visible blockers moved beyond `ui/events/keycode_converter.cc`, currently into Linux-only IPC/network code:
  ```text
  components/named_mojo_ipc_server/named_mojo_server_endpoint_connector_linux.cc
  services/network/public/cpp/network_interface_change_listener_mojom_traits.h: fatal error: 'linux/rtnetlink.h' file not found
  ```

## Files touched

- `cef/patch/patches/qnx/chromium/ui_events_keycode_converter_qnx_scan_codes.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/ui-events-keycode-converter-qnx-scan-codes.md`
- `cef/docs/qnx/build-error-index.md`
