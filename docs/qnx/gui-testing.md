# QNX QEMU GUI automation

## Goal

The QNX QEMU session can now be driven without a host GUI window. An agent can
capture the framebuffer, inject pointer/keyboard events, and execute a
repeatable JSON action sequence. Each action is machine-readable and a failed
scenario attempts to save a failure screenshot.

## Method investigation

The implementation was selected after checking the following approaches on
2026-07-16:

| Method | Use | Decision |
|---|---|---|
| QEMU QMP `input-send-event` | Absolute pointer events (`0..32767`) | **Primary pointer path**. It is already proven to reach QNX `SCREEN_EVENT_POINTER` through `virtio-tablet-pci`. |
| QEMU QMP `send-key` | QEMU qcode keyboard events | **Primary keyboard path**. ASCII text and key combinations are supported by the host tool. |
| QEMU QMP `screendump` | Host-side PNG/PPM framebuffer capture | **Primary observation path**. It does not require a VNC viewer or a desktop session. |
| QNX `screenshot` | Guest Screen capture copied over the existing NFS export | **Accelerated-display fallback**. `qnx_gui.py --source guest` converts the QNX BMP to PNG without extra host packages. |
| QNX `vncserv` | VNC stream and input forwarding into QNX Screen | **Optional fallback** when QNX Screen/VNC is available in the image. It is not required for the initial harness. |
| VNCDoTool | VNC mouse/keyboard, screenshots, image matching | Useful for generic VNC guests, but less deterministic here because the current runner uses QEMU `gtk`/`virgl` and QNX Screen is the platform display owner. |
| QEMU monitor GUI / `xdotool` | Host-window automation | Rejected. It depends on a host display, window focus, and pixel scaling, which is unsuitable for headless CI or an autonomous debugger. |

References:

- [QEMU QMP reference](https://qemu-project.gitlab.io/qemu/interop/qemu-qmp-ref.html)
  (`input-send-event`, `send-key`, and `screendump`)
- [QEMU QMP specification](https://www.qemu.org/docs/master/interop/qmp-spec.html)
- [QNX `vncserv`](https://www.qnx.com/developers/docs/8.0/com.qnx.doc.screen/topic/manual/vncserv.html)
- [VNCDoTool usage](https://vncdotool.readthedocs.io/en/latest/usage.html)
- [Microsoft Quicksand GUI input example](https://github.com/microsoft/quicksand/blob/main/examples/gui_input.py)

## Start a controllable QEMU session

Prepare TAP/NFS once after a host reboot:

```bash
sudo ./cef/tools/qnx_setup_env.sh
```

For a boot-and-mount session that stays alive:

```bash
./cef/tools/qnx_run.sh --gui --keep-qemu --mount-only \
  --qmp-socket /tmp/qnx-qmp.sock
```

For a GUI application started in the guest and left running:

```bash
./cef/tools/qnx_run.sh --gui --preload-system-egl --kill-existing \
  --detach --qconn-port 8000 -- \
  ./cefsimple --use-gl=egl --use-native \
  --ozone-platform=qnx --no-sandbox
```

Mesa EGL warnings are suppressed by default. Add `--show-egl-warnings` when
collecting graphics-provider diagnostics.

`--gui` is shorthand for `--qemu-graphics virgl --with-input`. The existing
render-only and headless paths are unchanged. The runner prints the QMP socket
path and the serial/app log paths.

## One-shot agent operations

The tool uses only the Python standard library:

```bash
# Capture and get dimensions from the result.
./cef/tools/qnx_gui.py --json screenshot \
  --output out/qnx_release/gui-artifacts/boot.png

# If accelerated virgl scanout is not available to QMP screendump, capture
# the QNX Screen surface through the already-mounted NFS path instead.
./cef/tools/qnx_gui.py --json screenshot --source guest \
  --serial-port 10024 --output out/qnx_release/gui-artifacts/guest.png

# Coordinates are guest display pixels; they are scaled to QMP's absolute range.
./cef/tools/qnx_gui.py --screen-size 1280x720 --json \
  click --x 640 --y 360
./cef/tools/qnx_gui.py --json key ctrl+l
./cef/tools/qnx_gui.py --json type 'https://example.test/'
./cef/tools/qnx_gui.py --json key ret

# Useful for health/debugging probes.
./cef/tools/qnx_gui.py --json status
./cef/tools/qnx_gui.py --json mice
```

The default socket is `/tmp/qnx-qmp.sock`; use `QNX_QMP_SOCK` or
`--socket PATH` to select another VM. `screenshot` should be run first when
the display size is unknown. Set `QNX_GUI_SCREEN_SIZE=WIDTHxHEIGHT` for a
stable test environment.

## Repeatable scenarios

Scenarios are JSON so an agent can generate, review, and replay them without
embedding shell quoting in a command line. See
`tools/qnx_gui.example.json`:

```json
{
  "name": "smoke",
  "screen_size": "1280x720",
  "actions": [
    {"action": "screenshot", "path": "01-before.png"},
    {"action": "click", "x": 640, "y": 360},
    {"action": "type", "text": "hello"},
    {"action": "key", "key": "ret"},
    {"action": "sleep", "seconds": 1},
    {"action": "screenshot", "path": "02-after.png"}
  ]
}
```

Run it with:

```bash
./cef/tools/qnx_gui.py --json run tools/qnx_gui.example.json \
  --artifact-dir out/qnx_release/gui-artifacts/smoke
```

Supported actions are `screenshot`, `move`, `click`, `key`, `type`, `sleep`,
and `wait_for_change`. On failure the result contains the failed action and a
best-effort `failure-NNN.png`. A non-zero exit code makes the scenario usable
in CI.

## Debugging contract and limitations

- The QMP socket is a host control interface. Do not expose it on TCP or a
  shared machine; it has unrestricted VM control.
- Pointer coordinates are scaled from the supplied display size. Wrong
  dimensions produce deterministic but wrong clicks, so record the dimensions
  from the first screenshot or configure `QNX_GUI_SCREEN_SIZE`.
- `type` intentionally rejects unsupported Unicode instead of silently losing
  characters. Use QMP qcodes through `key` for special keys.
- `wheel-up`/`wheel-down` are exposed for completeness, but the current
  `virtio-tablet-pci` path does not reliably surface wheel properties in QNX
  Screen. Treat wheel scenarios as unsupported until a device/image change is
  validated.
- QMP `screendump` captures QEMU's display surface. With an accelerated
  virgl scanout, use `--source guest` if the QMP image is blank or QEMU reports
  that the display surface cannot be dumped. The guest path requires the
  existing NFS mount and an idle serial connection.
- QEMU and QNX boot failures still belong in the serial/boot logs. The GUI
  harness is an observation and input layer, not a replacement for the
  `qnx_run.sh` login/NFS setup.
