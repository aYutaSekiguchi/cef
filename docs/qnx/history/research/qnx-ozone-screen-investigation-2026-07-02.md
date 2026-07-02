# QNX Ozone / Screen Rendering Investigation

> Date: 2026-07-02
> Scope: Planning/research note only.  No Chromium/Ozone implementation has been started from this note.

## Goal

Investigate how to add a QNX Ozone implementation that can perform real
on-screen rendering, starting with x86_64 QEMU while keeping the design portable
to a future aarch64 board target.

Minimum research requested:

- inspect the local QNX SDK/sysroot under `~/qnx800/`
- check whether qnx-ports libraries are usable
- perform QNX/WebKit/graphics research, especially how existing QNX browser or
  WebKit-like ports render

## Current conclusion

The strongest first implementation direction is a **native QNX Screen +
EGL/GLES2 Ozone platform**, not DRM/GBM and not a mandatory Wayland/Weston
runtime dependency.

Rationale:

- the local SDK has Screen, EGL, GLES2/GLES3, OpenVG, Vulkan, Wayland, and
  xkbcommon for both `x86_64` and `aarch64le`
- the local SDK does **not** contain GBM or DRM headers/libs
- QNX EGL headers define `EGLNativeWindowType` as `screen_window_t`
- QNX provides `EGL_QNX_platform_screen` / `EGL_PLATFORM_SCREEN_QNX`
- qnx-ports examples and available ports point toward Screen/EGL as the native
  QNX drawing model
- QNX's historical browser/WebKit/Blink precedent appears to use QNX-native
  graphics abstractions rather than Linux DRM/GBM

## Local SDK/sysroot findings

Checked root:

```text
/home/yuta/qnx800
```

Important paths:

| Item | Path | Status |
|---|---|---|
| SDK root | `/home/yuta/qnx800` | present |
| target sysroot | `/home/yuta/qnx800/target/qnx` | present |
| host tools | `/home/yuta/qnx800/host/linux/x86_64` | present |
| `qcc` | `/home/yuta/qnx800/host/linux/x86_64/usr/bin/qcc` | present |
| `q++` | `/home/yuta/qnx800/host/linux/x86_64/usr/bin/q++` | present |
| `wayland-scanner` | `/home/yuta/qnx800/host/linux/x86_64/usr/bin/wayland-scanner` | present |
| x86_64 gdb | `x86_64-nto-qnx8.0.0-gdb-14.2` | present |
| aarch64 gdb | `aarch64-nto-qnx8.0.0-gdb-14.2` | present |

Architecture library directories found:

- `/home/yuta/qnx800/target/qnx/x86_64/usr/lib`
- `/home/yuta/qnx800/target/qnx/aarch64le/usr/lib`

### Headers present

| Area | Headers found |
|---|---|
| Screen | `screen/screen.h`, `screen/screen_ext.h`, `input/screen_helpers.h` |
| EGL/GLES | `EGL/egl.h`, `EGL/eglext.h`, `EGL/eglplatform.h`, `GLES2/gl2.h`, `GLES3/gl3.h`, `KHR/khrplatform.h` |
| Vulkan | `vulkan/vulkan.h`, `vulkan/vulkan_screen.h`, `vulkan/vulkan_wayland.h` |
| Wayland | `wayland-client.h`, `wayland-server.h`, `wayland-egl.h`, `wayland-cursor.h` |
| xkbcommon | `xkbcommon/xkbcommon.h` |

### Headers not found in the SDK sysroot

- `gbm.h`
- `libdrm/drm.h` / `drm/drm.h`
- `fontconfig/fontconfig.h`
- `freetype2/ft2build.h`
- `harfbuzz/hb.h`
- `X11/Xlib.h`
- GTK headers
- Qt headers

### Libraries present for both x86_64 and aarch64le

| Area | Libraries |
|---|---|
| Screen | `libscreen.so`, `libscreen.so.1` |
| EGL/GLES | `libEGL.so`, `libGLESv2.so` |
| OpenVG | `libOpenVG.so`, `libOpenVGU.so` |
| Wayland | `libwayland-client.so`, `libwayland-server.so`, `libwayland-egl.so`, `libwayland-cursor.so` |
| Vulkan | `libvulkan.so` |
| Input/keymap | `libxkbcommon.so` |
| QNX fd compatibility | `libepoll.so`, `libeventfd.so`, `libtimerfd.so`, `libsignalfd.so` |

### Libraries not found in the SDK sysroot

- `libdrm*`
- `libgbm*`
- `libfontconfig*`
- `libfreetype*`
- `libharfbuzz*`
- `libQt*`
- `libgtk*`

No relevant `.pc` pkg-config files were found for Screen/EGL/GLES/Wayland/DRM/GBM/fontconfig/Qt/GTK in the sysroot scan.

## QNX Screen/EGL details relevant to Ozone

Important local header facts:

- `EGL/eglplatform.h` maps QNX `EGLNativeWindowType` to
  `struct _screen_window*`, i.e. `screen_window_t`.
- `EGL/eglext.h` defines:
  - `EGL_QNX_platform_screen`
  - `EGL_PLATFORM_SCREEN_QNX 0x3550`
- `screen/screen.h` provides:
  - `screen_create_context`
  - `screen_create_window`
  - `screen_create_window_buffers`
  - `screen_post_window`
  - `screen_blit`
  - `screen_create_event`
  - `screen_get_event`
  - `SCREEN_USAGE_OPENGL_ES2`
  - `SCREEN_USAGE_OPENGL_ES3`
  - `SCREEN_USAGE_NATIVE`
  - pointer, keyboard, close, and multitouch event types

This supports an Ozone design where:

1. `PlatformWindow` owns a `screen_window_t`.
2. `SurfaceFactoryOzone` / `GLOzoneEGL` exposes an EGL window surface backed by
   that `screen_window_t`.
3. `eglSwapBuffers()` posts the rendered content through Screen.
4. Initial `PlatformScreen` can be simple/fixed-size, then later enumerate real
   Screen displays.
5. Input can start as minimal Screen event translation and expand later.

## x86_64 and aarch64 link check

A minimal probe including Screen/EGL/GLES2 symbols was compiled and linked for
both target architectures:

```sh
qcc -Vgcc_ntox86_64 \
  -o /tmp/qnx_screen_egl_probe_gcc_ntox86_64 \
  /tmp/qnx_screen_egl_probe.c \
  -lscreen -lEGL -lGLESv2

qcc -Vgcc_ntoaarch64le \
  -o /tmp/qnx_screen_egl_probe_gcc_ntoaarch64le \
  /tmp/qnx_screen_egl_probe.c \
  -lscreen -lEGL -lGLESv2
```

Both links succeeded:

- x86_64 output: QNX x86-64 ELF executable
- aarch64le output: QNX aarch64 ELF executable

Implication: the first Ozone design should be architecture-neutral around QNX
Screen/EGL APIs.  The same source-level backend should be usable for x86_64
QEMU and future aarch64 boards, with target CPU/build-script support handled
separately.

## QEMU-relevant Screen assets in the SDK

Target utilities found under the x86_64 sysroot include:

- `x86_64/sbin/screen`
- `x86_64/usr/bin/egl-configs`
- `x86_64/usr/bin/gles2-gears`
- `x86_64/usr/bin/gles2-maze`
- `x86_64/usr/bin/gles2-teapot`
- `x86_64/usr/bin/gles3-gears`
- `x86_64/usr/bin/screenshot`
- `x86_64/usr/bin/vulkaninfo`
- `x86_64/usr/bin/wayland-info`

Screen config files found:

```text
/home/yuta/qnx800/target/qnx/usr/share/screen/graphics-headless.conf
/home/yuta/qnx800/target/qnx/usr/share/screen/graphics-virtual-display.conf
```

`graphics-headless.conf` is a minimal Screen configuration:

```text
begin winmgr
  begin globals
    alloc-config = stdbuf
    blit-config = sw
  end globals
end winmgr
```

`graphics-virtual-display.conf` defines a software virtual display:

```text
begin virtual display
  id_string = virtual-1
  video-mode = 1280 x 768 @ 60
  format = rgba8888
  usage =  sw
end virtual display
```

These files suggest that x86_64 QEMU may be able to validate at least a
software/virtual-display Screen path before hardware-board validation.

## qnx-ports findings

Primary source:

- `https://github.com/qnx-ports`
- `https://github.com/qnx-ports/build-files`
- QNX open-source dashboard: `https://oss.qnx.com/`

The qnx-ports organization points users to `build-files` as the place where
ported projects have per-port build instructions.

Relevant ports/libraries observed or identified as likely relevant:

- `weston`
- `wayland`
- `qt`
- `SDL`
- `gtk`
- `cairo`
- `pixman`
- `fontconfig`
- `freetype`
- `harfbuzz`
- `libxkbcommon`
- `webrtc`
- `pdfium`

No qnx-ports WebKit/WPE/QtWebKit port was found during this pass.

### Interpretation

- qnx-ports libraries are useful as references and optional future dependency
  sources.
- They should not be introduced as Chromium runtime dependencies without an
  explicit dependency/staging decision.
- The current Chromium/CEF QNX port already prefers bundled Chromium libraries
  for many components, e.g. bundled fontconfig due to SDP/system fontconfig
  issues.

## WebKit / existing browser rendering research

No maintained qnx-ports WebKit/WPE port was found.

Relevant precedent instead comes from:

1. QNX historical WebKit/browser documentation, which confirms QNX browser
   technology existed but is old/deprecated and not a direct Chromium Ozone
   implementation guide.
2. QNX/Blink browser documentation snippets mentioning `ozone-qnx`, suggesting
   that a commercial QNX Chromium/Blink browser used a QNX-native Ozone backend
   over Screen/input services.
3. Qt QNX platform plugin behavior, relevant for QtWebKit/QtWebEngine-style
   rendering:
   - accelerated windows use QNX Screen windows with `SCREEN_USAGE_OPENGL_ES2`
     or `SCREEN_USAGE_OPENGL_ES3`
   - EGL surfaces are created from the Screen native window
   - raster windows use Screen render buffers and `screen_post_window` / blit
4. qnx-ports Weston QNX backend behavior:
   - creates Screen windows
   - defaults to EGL/GLES2 usage
   - has a Screen backend and optional pixman/software route

Conclusion: QNX browser/UI ports generally render through **QNX Screen**,
either via EGL/GLES for accelerated rendering or Screen buffers/blit/post for
software rendering.

## Chromium/Ozone codebase implications

Current QNX CEF build is Ozone-enabled but not using a visual platform:

- `use_ozone = true`
- `ozone_platform_wayland = false`
- `ozone_platform_x11 = false`
- `ozone_platform_drm = false`
- `use_x11 = false`
- `use_glib = false`
- `use_gio = false`
- `use_gtk = false`
- `use_dbus = false`
- `use_pangocairo = false`
- `use_xkbcommon = false`
- Dawn/WebGPU/Vulkan-related build paths are intentionally disabled for the
  current QNX baseline.

Relevant Chromium/Ozone areas to inspect for implementation:

- `docs/ozone_overview.md`
- `build/config/ozone.gni`
- `build/config/ozone_extra.gni`
- `ui/ozone/BUILD.gn`
- `ui/ozone/platform/headless/`
- `ui/ozone/platform/cast/`
- `ui/ozone/common/gl_ozone_egl.*`
- `ui/platform_window/`
- `ui/events/ozone/`

Useful implementation models:

- `ui/ozone/platform/headless`: smallest Ozone skeleton and simple
  `PlatformWindow` / `PlatformScreen` patterns.
- `ui/ozone/platform/cast`: compact EGL/native-window pattern.
- `ui/ozone/platform/wayland`: useful for comparison, but likely too much
  Linux/desktop dependency surface for the first QNX path.

## Architecture options

### Option A — Native QNX Screen + EGL/GLES2 Ozone backend

Recommended first path.

Expected pieces:

- `OzonePlatformQnx`
- `QnxWindow` implementing `PlatformWindow` and owning `screen_window_t`
- QNX Screen event source / minimal input translation
- `QnxSurfaceFactory` / `GLOzoneEGLQnx`
- EGL window surface creation from `screen_window_t`
- simple `PlatformScreen` initially, real Screen display enumeration later
- stubs for overlays/GPU platform support/input controller where acceptable for
  the first milestone

Pros:

- matches QNX native rendering model
- works with SDK APIs present for both x86_64 and aarch64le
- avoids DRM/GBM and desktop Linux assumptions
- avoids requiring Weston as a runtime dependency

Cons / unknowns:

- Screen service and EGL runtime availability in QEMU still need validation
- input/event/cursor/display integration must be written
- ANGLE/system EGL integration details need investigation

### Option B — Screen software canvas first

Possible fallback or early proof-of-pixels path.

Expected approach:

- create Screen window buffers with CPU read/write/native usage
- render/copy into Screen buffer memory
- post with `screen_post_window`

Pros:

- may work in x86_64 QEMU with virtual/software display even if GLES path is
  limited
- useful diagnostic fallback

Cons:

- may not exercise Chromium's GPU/compositor path
- performance is poor
- can become a detour if the actual goal is accelerated CEF rendering

### Option C — Wayland-on-Screen via QNX Weston

Secondary option only.

Pros:

- QNX SDK has Wayland libraries and qnx-ports has Weston
- Chromium already has a mature Wayland Ozone backend

Cons:

- requires a compositor runtime dependency
- Chromium Wayland path has Linux/pkg-config/xkb/desktop assumptions
- current QNX GN intentionally disables Wayland and related desktop deps
- QNX docs warn that Screen windows and Wayland windows should not be mixed in
  some compositor contexts

### Option D — DRM/GBM/minigbm

Not recommended for current first milestone.

Reasons:

- local SDK has no DRM/GBM headers/libs
- Chromium DRM Ozone is ChromeOS-oriented
- current QNX config disables DRM

## Immediate next checks before implementation

1. Boot/mount x86_64 QEMU and verify Screen service availability:

```sh
pidin ar | grep screen
ls /dev/screen /usr/lib/graphics /usr/share/screen 2>/dev/null
```

2. Try QNX-provided Screen/EGL utilities in the guest:

```sh
egl-configs
gles2-gears
gles2-teapot
screenshot
```

3. Try starting Screen with the virtual display config if it is not already
running:

```sh
screen -c /usr/share/screen/graphics-virtual-display.conf
```

Exact command-line options should be confirmed on the target (`screen --help` or
QNX docs), because service startup may be image-specific.

4. Build and run a tiny Screen/EGL clear-color probe on QNX before adding
Chromium/Ozone code.

5. Only after runtime Screen/EGL is confirmed, plan the Ozone implementation.

## Implementation planning tasks

When moving from research to planning, answer these before writing code:

1. Should QNX Ozone be added in-tree under `ui/ozone/platform/qnx`, or wired as
   an external Ozone platform through `ozone_extra.gni`?
2. What is the first milestone?
   - Screen/EGL clear-color window
   - minimal Chromium Ozone platform build
   - `cefsimple --ozone-platform=qnx` visible output
3. Should software Screen canvas be implemented as a fallback in the first
   milestone, or deferred until after EGL path validation?
4. What is the target validation environment?
   - x86_64 QEMU virtual display
   - a real x86_64 target
   - future aarch64 board
5. How should GN args change?
   - add `ozone_platform_qnx`
   - set `ozone_platform = "qnx"`
   - avoid accidental X11/Wayland defaults from GN-level `is_linux`
6. What minimum input/cursor/display support is required for the first usable
   `cefsimple` run?

## Risks and unknowns

| Risk | Severity | Notes |
|---|---|---|
| QEMU Screen/EGL runtime unavailable | High | Headers/libs exist, but guest image/runtime service still must be validated. |
| EGL/GLES only works on board, not QEMU | High | May require software Screen fallback or board-first validation. |
| ANGLE/EGL integration mismatch | High | Need decide whether Chromium uses ANGLE EGL path, system EGL directly, or both. |
| GN `is_linux` vs C++ `IS_LINUX` split | High | QNX may select Linux-like build files but must not run Linux-only code paths. |
| Input/events/cursor work expands scope | Medium | Initial render can stub/minimize, but usable browser needs Screen event mapping. |
| Wayland path dependency creep | Medium | Weston/Wayland could work but changes deployment and dependency assumptions. |
| aarch64 board support | Medium | SDK parity is good, but build scripts currently default x64 and board runtime differs. |
| External qnx-ports dependencies | Medium | Useful references, but adding dependencies needs explicit policy. |

## Suggested first validation sequence

```sh
# 1. Confirm current repo baseline after any changes.
cd /home/yuta/chromium/src
./cef/tools/qnx_sync_sources.sh -f -R
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800
./out/qnx_release/ninja_qnx.sh base_unittests

# 2. Boot QNX guest and inspect Screen runtime.
sudo ./cef/tools/qnx_setup_env.sh
./cef/tools/qnx_run_test.sh --mount-only --kill-existing

# 3. In guest, check Screen/EGL tools.
pidin ar | grep screen || true
egl-configs
gles2-gears
screenshot

# 4. Build/run a tiny Screen/EGL probe before Ozone code.
# Link flags should include: -lscreen -lEGL -lGLESv2

# 5. Only then design and implement the first QNX Ozone milestone.
```

## Current recommendation

Proceed with a staged plan centered on native QNX Screen:

1. Validate Screen/EGL runtime in x86_64 QEMU.
2. If EGL works, design `ui/ozone/platform/qnx` around Screen windows and EGL
   window surfaces.
3. If EGL does not work in QEMU but Screen software/virtual display works, add a
   software Screen probe/fallback decision before deep Chromium work.
4. Keep the implementation arch-neutral so the same backend can target future
   aarch64 boards.
5. Treat Wayland/Weston and qnx-ports GUI stacks as references or fallback
   experiments, not first-class dependencies for the initial Ozone backend.

## QEMU with GUI runtime probe (2026-07-02)

A temporary GUI runner was created outside the repository by copying
`tools/qnx_run.sh` to `/tmp/qnx_run_gui.sh` and replacing QEMU's `-nographic`
argument with `-display gtk`.  No repository script was changed.

Host/QEMU state:

- Host session had `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`, and QEMU reported
  display backends including `gtk`, `sdl`, `egl-headless`, `curses`, and `dbus`.
- QEMU launched successfully with:

```text
qemu-system-x86_64 ... -display gtk ... -serial tcp:127.0.0.1:10025,server,nowait ...
```

Guest state:

- QNX booted and serial login succeeded:

```text
QNX qnxqemu 8.0.0 2025/07/30-19:24:08EDT x86pc x86_64
```

- The image attempted to start Screen during boot, but initial startup reported:

```text
---> Starting Screen...
Unable to access /dev/screen/
/system/etc/startup/post_startup.sh[56]: cannot create /dev/screen/command: No such file or directory
```

- Starting Screen manually with the virtual-display config succeeded enough to
  create `/dev/screen`:

```sh
screen -c /usr/share/screen/graphics-virtual-display.conf
```

After this, `/dev/screen` contained entries such as `0`, `buffers`, `command`,
`gpus`, `input`, `requests`, and `state.tar`.

### EGL/GLES result

The bundled EGL/GLES utilities did not work on this QEMU virtual-display setup:

```sh
egl-configs
gles2-gears
```

Both reported:

```text
eglGetDisplay: an EGLDisplay argument does not name a valid EGLDisplay
```

This means that, in the current x86_64 QEMU image/configuration, Screen itself
can run but no valid EGL display is exposed by the active Screen configuration.

### Software Screen rendering result

A temporary host-side test program (`/tmp/qnx_screen_fill.c`) was compiled into
`out/qnx_release/qnx_screen_fill` for x86_64.  It used only Screen software
buffer APIs:

- `screen_create_context()`
- `screen_create_window()`
- `SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE | SCREEN_USAGE_READ`
- `SCREEN_FORMAT_RGBA8888`
- `SCREEN_PROPERTY_RENDER_BUFFERS`
- `SCREEN_PROPERTY_POINTER`
- `SCREEN_PROPERTY_STRIDE`
- `screen_post_window()`

Running it in the guest posted a 640x360 gradient window at `(80, 80)`.  A QNX
`screenshot` capture succeeded:

```text
/home/yuta/chromium/src/out/qnx_release/qnx-screen-fill.bmp
```

Host-side image analysis showed:

```text
image size: 1280x768
non-black bbox: (80, 80) - (719, 439)
non-black pixels: 230400
```

This confirms that **QEMU with GUI + QNX Screen virtual display can display
software-rendered Screen windows**.  It does **not** yet confirm EGL/GLES
rendering.

### Implications

- QEMU GUI boot and Screen software rendering are viable for early pixel-output
  validation.
- The first QEMU milestone may need a software Screen path (`screen_post_window`)
  before accelerated EGL/GLES is available.
- The native Screen/EGL Ozone design remains appropriate for real targets and
  future aarch64 boards, but the QEMU image needs additional EGL display setup
  or a different graphics configuration before GLES utilities can validate the
  accelerated path.

### Immediate follow-up

Investigate how to enable a valid EGL display in the x86_64 QEMU image/config:

1. inspect QNX Screen docs and installed examples for `khronos` / EGL display
   configuration;
2. inspect `screen` startup configuration and boot scripts in the guest image;
3. check whether QEMU needs a different virtual GPU/device, graphics driver, or
   Screen config beyond `graphics-virtual-display.conf`;
4. if EGL cannot be enabled in QEMU, plan a software Screen Ozone milestone for
   QEMU and reserve EGL validation for hardware/aarch64 board targets.

## QEMU EGL display enablement follow-up (2026-07-02)

The EGL failure above was reproduced with a GUI window but **without** a QEMU
virtio GPU device.  Further local inspection shows that EGL display enablement
for the existing QEMU image is tied to the image's virtio/virgl Screen stack,
not merely to replacing `-nographic` with a GUI display backend.

### QNX Screen configuration requirements

QNX Screen documentation describes EGL/GLES enablement through the `khronos`
section of `graphics.conf`:

```text
begin khronos
  begin egl display 1
    egl-dlls = ...
    glesv2-dlls = ...
    gpu-dlls = ...
  end egl display
  begin wfd device 1
    wfd-dlls = ...
  end wfd device
end khronos
```

The simple SDK configs used earlier are insufficient for EGL:

- `graphics-headless.conf` has only `alloc-config = stdbuf` and `blit-config = sw`.
- `graphics-virtual-display.conf` defines a software virtual display, but no
  `khronos`, `egl display`, GPU library, or WFD device section.

Therefore `screen -c /usr/share/screen/graphics-virtual-display.conf` can expose
software Screen windows but cannot expose a valid EGL display.

### mkqnximage / package findings

QNX's `mkqnximage` documentation exposes `--graphics=[yes|no]`, but also notes
that official graphics support is limited by VM type.  Local mkqnximage scripts
are still useful because they show the intended QEMU graphics path:

- `qemu/runimage` uses:

```text
-vga none -device virtio-vga-gl -display sdl,gl=on
```

when `OPT_GRAPHICS=yes`.

- `inputs/system_files_virtio-drm` contains the expected EGL/WFD Screen config:

```text
begin egl display 1
  egl-dlls = libglapi-mesa.so libEGL-mesa.so
  glesv2-dlls = libglapi-mesa.so libGLESv2-mesa.so
  gpu-dlls = gpu_drm-virtio.so
end egl display

begin wfd device 1
  wfd-dlls = libwfdcfg-virtio-generic.so libWFDvirtio-drm.so
  pipeline1-display = 1
end wfd device
```

However, the local `~/qnx800` package set does **not** include the official
`Screen Utilities` or `Screen Board Support VirtIO` packages required by a clean
`mkqnximage --graphics=yes --build`:

```text
--graphics=yes requires the "Screen Utilities" package
--graphics=yes requires the "Screen Board Support VirtIO" package
```

Direct sysroot searches also found no official `usr/lib/graphics/virtio-drm/`
or `usr/lib/graphics/vmwgfx-drm/` driver assets under `~/qnx800/target/qnx`.

### Existing QEMU image custom graphics stack

The existing QEMU image under:

```text
/home/yuta/qnx800/images/qemu/qemu
```

has `OPT_GRAPHICS='no'` in `local/options`, but also has local custom snippets
that start a virtio Screen stack:

```text
local/snippets/post_start.~20.graphics
screen -u __SCREEN_ID__:__SCREEN_ID__ \
  -c /usr/lib/graphics/drm-virtio/graphics-virtio-virgl.conf
```

The same image has a custom QEMU option snippet:

```text
-smp 8 -vga none -device virtio-vga-gl -display sdl,gl=on
```

The repository's `tools/qnx_run.sh` does not consume that mkqnximage run script
or snippet; it launches QEMU directly with `-nographic`.  The temporary first GUI
probe changed only the display backend, so QNX still had no virtio GPU device for
`graphics-virtio-virgl.conf` to bind to.  That explains the boot-time Screen
failure and the invalid EGL display result.

### Successful EGL/GLES validation with virtio-vga-gl

A second temporary runner was created outside the repository:

```text
/tmp/qnx_run_virgl.sh
```

It is a copy of `tools/qnx_run.sh` with the QEMU graphics portion changed from
`-nographic` to:

```text
-vga none
-device virtio-vga-gl
-display gtk,gl=on
```

With that QEMU device enabled, the guest booted and Screen started with the
virtio/virgl config:

```text
screen -u 36:36 -c /usr/lib/graphics/drm-virtio/graphics-virtio-virgl.conf
```

Runtime checks showed:

```text
/dev/screen present
screen process present
drm-virtio process present
fullscreen-winmgr process present
```

`egl-configs` then succeeded and reported a Mesa EGL display:

```text
EGL_VENDOR = Mesa Project
EGL_VERSION = 1.5
EGL_CLIENT_APIS = OpenGL_ES
EGL_EXTENSIONS = ... EGL_QNX_image_native_buffer ... EGL_QNX_api_trace
```

`gles2-gears` was also launched successfully long enough to capture a screenshot:

```text
/home/yuta/chromium/src/out/qnx_release/qnx-gles2-gears.bmp
```

Screenshot validation:

```text
image size: 1280x768
sample unique colors: 1162
channel min/max: 0/255
channel stdev: 110.61
```

This confirms that **the x86_64 QEMU image can expose a working Screen EGL/GLES
display when QEMU is launched with virtio-vga-gl and host GL enabled**.

### Updated implications

- QEMU is viable for the first accelerated Screen/EGL validation milestone.
- The earlier invalid-EGL result was caused by launching the guest without the
  virtio GL device required by the image's custom Screen configuration.
- A software Screen path remains useful as a fallback/diagnostic, but it is no
  longer the only QEMU pixel-output path.
- Durable follow-up has started by parameterizing `tools/qnx_run.sh`; use
  `--qemu-graphics virgl` or `--virgl` instead of keeping
  `/tmp/qnx_run_virgl.sh` as the validation entry point.
- For clean mkqnximage reproduction outside this custom image, the missing
  official Screen utilities / VirtIO board-support packages still need to be
  installed or supplied.

### Recommended next step before implementation

Do not start `ui/ozone/platform/qnx` yet.  The QEMU launch path now has an
explicit opt-in GUI/virgl mode:

```sh
./cef/tools/qnx_run.sh --virgl -- egl-configs
./cef/tools/qnx_run.sh --qemu-graphics virgl -- gles2-gears
```

Use this path next to run a minimal Screen/EGL clear-color probe compiled with
`-lscreen -lEGL -lGLESv2`.
