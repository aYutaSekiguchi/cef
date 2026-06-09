# PDFium needs a QNX-specific fxge PlatformIface implementation

- Date: 2026-06-09
- Signature: `"Included on the wrong platform"`
- Stage: compile
- Category: feature-guard
- Scope: `third_party/pdfium/core/fxge`
- External reference: `qnx-ports/build-files`, `ports/pdfium/patches/pdfium/0002-Implement-CFX_GEModule-PlatformIface-for-QNX.patch`

## Symptoms

After the Crashpad portability fixes, `cefsimple` advanced to PDFium:

```text
FAILED: obj/third_party/pdfium/core/fxge/fxge/fx_linux_impl.o
../../third_party/pdfium/core/fxge/linux/fx_linux_impl.cpp:23:2: error: "Included on the wrong platform"
```

## Root cause

- `third_party/pdfium/core/fxge/BUILD.gn` selected `linux/fx_linux_impl.cpp` when `is_linux || is_chromeos`.
- In this Chromium/CEF QNX port, GN may make QNX Linux-like for source selection (`is_linux && is_qnx`), while C++ `BUILDFLAG(IS_LINUX)` remains false for QNX.
- The Linux translation unit then rejected QNX via its TU-local guard:
  ```cc
  #if !BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CHROMEOS) && !defined(OS_ASMJS)
  #error "Included on the wrong platform"
  #endif
  ```

## Review against QNX Ports

An initial local fix allowed QNX through `fx_linux_impl.cpp`'s guard. That unblocked compilation, but review against the public QNX Ports PDFium port showed a better durable shape:

- QNX Ports adds a dedicated file:
  - `core/fxge/qnx/fx_qnx_impl.cpp`
- QNX Ports wires it from `core/fxge/BUILD.gn`:
  ```gn
  if (is_qnx) {
    sources += [ "qnx/fx_qnx_impl.cpp" ]
  }
  ```

This preserves a QNX-native hook point and avoids silently inheriting future Linux-only changes in `fx_linux_impl.cpp`.

## Fix pattern

- Do **not** keep QNX in the Linux implementation guard.
- Stop selecting `linux/fx_linux_impl.cpp` for QNX when GN reports both `is_linux` and `is_qnx`.
- Add QNX's own `qnx/fx_qnx_impl.cpp`, based on the same generic folder-font implementation but with QNX-specific class names and guard.

## Applied change

- `third_party/pdfium/core/fxge/BUILD.gn`
  ```diff
  -  if (is_linux || is_chromeos) {
  +  if ((is_linux && !is_qnx) || is_chromeos) {
       sources += [ "linux/fx_linux_impl.cpp" ]
     }

  +  if (is_qnx) {
  +    sources += [ "qnx/fx_qnx_impl.cpp" ]
  +  }
  ```
- New managed file:
  - `cef/patch/qnx/chromium/new_files/third_party/pdfium/core/fxge/qnx/fx_qnx_impl.cpp`
- Removed obsolete patch:
  - `cef/patch/patches/qnx/chromium/pdfium_fx_linux_impl_qnx_guard.patch`

## Verification

- Clean-tree reset plus bootstrap succeeded:
  - `git checkout -f`: `0`
  - `gclient sync -f -R`: `0`
  - `./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800`: `0`
- Post-bootstrap source selection is correct:
  - `third_party/pdfium/core/fxge/linux/fx_linux_impl.cpp` has no QNX guard extension (`BUILDFLAG(IS_QNX)` hits: `0`)
  - `third_party/pdfium/core/fxge/qnx/fx_qnx_impl.cpp` exists
  - `BUILD.gn` selects `linux/fx_linux_impl.cpp` only for `(is_linux && !is_qnx) || is_chromeos`
  - `BUILD.gn` selects `qnx/fx_qnx_impl.cpp` for `is_qnx`
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` confirms PDFium is passed:
  - `fx_linux_impl.cpp: "Included on the wrong platform"` hits: `0`
  - `fx_linux_impl.o` failures: `0`
  - `fx_qnx_impl` appears in the build log
- The next blocker remains past PDFium, in `ui/events`:
  ```
  FAILED: obj/ui/events/dom_keycode_converter/keycode_converter.o
  ../../ui/events/keycodes/dom/keycode_converter.cc:43:2: error: Unsupported platform
  ```

## Files touched

- `cef/patch/patches/qnx/chromium/pdfium_fx_qnx_impl.patch`
- `cef/patch/qnx/chromium/new_files/third_party/pdfium/core/fxge/qnx/fx_qnx_impl.cpp`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/pdfium-fx-qnx-platform-impl.md`
- `cef/docs/qnx/build-error-index.md`
