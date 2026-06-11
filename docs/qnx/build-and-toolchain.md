# QNX Build and Toolchain

## Recommended bootstrap flow

Use the CEF-managed source-sync helper first, then run the bootstrap script:

**Important:** For QNX® SDP 8.0, you must install "Notification FD Interfaces" from QNX Software Center to ensure `libeventfd` is available. This is required for builds that link against eventfd.

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_sync_sources.sh
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
```

`qnx_sync_sources.sh` is responsible for:

1. applying the CEF-managed `.gitmodules` / `DEPS` QNX source-sync patch
2. running `gclient sync` from the Chromium root so DEPS-managed QNX sources are present

`cef_create_projects_qnx.sh` is responsible for:

1. installing QNX-specific new files from `cef/patch/qnx/chromium/new_files/`
2. applying QNX patch sets from `cef/patch/patches/qnx/`
3. applying source-repo-local fixes for already-synced dependencies such as `third_party/epoll/src`
4. generating `out/qnx_release/args.gn`
5. generating `out/qnx_release/qnx_env.sh` and `out/qnx_release/ninja_qnx.sh`
6. running `gn gen`

## Default environment

| Item | Value |
|---|---|
| target tag | `147.0.7727.147` |
| QNX SDP root | `<QNX_SDP_ROOT>` |
| build dir | `out/qnx_release` |
| compile | Chromium bundled clang |
| link | QNX `qcc` |
| sysroot | `$QNX_TARGET` |
| libc++ | QNX SDK libc++ |

## Toolchain design

| Item | Configuration |
|---|---|
| compile | Chromium bundled clang (`third_party/llvm-build/`) |
| link | QNX QCC (`$QNX_HOST/usr/bin/qcc`) |
| libc++ | QNX SDK bundled libc++ (`use_custom_libcxx = false`) |
| sysroot | `$QNX_TARGET` |

### Key compile flags

```text
--target=x86_64-unknown-nto
-D__QNXNTO__ -D__QNX__ -DQNX_LIBM_BUILTINS
-D__LITTLEENDIAN__ -D__EXT_XOPEN_EX -D_POSIX_C_SOURCE=200809L
-Uisinf -Uisnan
--sysroot=$QNX_TARGET
-I<shim> -I$QNX_TARGET/usr/include/c++/v1
-include time.h
-include qnx_std_polyfill.h
```

### Key link flags

```text
-Vgcc_ntox86_64_cxx
--start-group {{rlibs}} {{libs}} --end-group
```

The `--start-group/--end-group` wrapper is important for QCC single-pass link behavior, especially with mixed Rust and C++ static libraries.

## GN configuration

Important arguments in `args.gn`:

```gn
target_os = "qnx"
target_cpu = "x64"
use_custom_libcxx = false
use_custom_libcxx_for_host = true
use_thin_lto = false
thin_lto_enable_optimizations = false
use_ozone = true
ozone_platform_wayland = false
ozone_platform_x11 = false
ozone_platform_drm = false
chrome_pgo_phase = 0
v8_enable_sandbox = false
```

The release bootstrap also lowers symbol levels and disables unsupported desktop/media integrations.

## OS detection model

At the GN level:

```text
is_linux = current_os == "linux" || is_qnx
```

At the C++ level:

- `BUILDFLAG(IS_LINUX)` must stay false on QNX
- branch with `BUILDFLAG(IS_QNX)` in code

This split is intentional. GN reuses much of the Linux file selection logic, while C++ code must avoid Linux-only runtime behavior such as `prctl()` assumptions.

## libc++ strategy

The port intentionally uses the QNX SDK libc++ instead of Chromium's in-tree libc++.

Reasons:

- avoids sysroot/header conflicts
- matches the QNX SDK runtime environment
- keeps ABI expectations aligned with the target platform

Missing library features are handled with targeted polyfills in:

```text
build/config/qnx/qnx_std_polyfill.h
```

## Process-launch strategy

QNX cannot safely rely on `fork()` from multithreaded Chromium processes. The port therefore uses a spawn-based implementation:

- `base/process/launch_qnx.cc`
- `posix_spawn()` / `posix_spawnp()`
- QNX-specific handling for fd remapping and current-directory behavior

## Important constraints

| Constraint | Why it matters |
|---|---|
| Do not force `BUILDFLAG(IS_LINUX)=1` | leads to Linux-only runtime paths and potential fatal crashes |
| Do not put global `use_lld=false` in args | breaks host-side Rust and tool builds |
| Do not rely on unmanaged root patches | long-term state must be reproducible from `cef/patch/...` |
| Do not bypass `qnx_env.sh` / `ninja_qnx.sh` | QNX builds require the SDK environment during `ninja` as well as `gn gen` |

## Patch maintenance workflow

Use this workflow whenever a QNX fix must survive beyond the current working tree.

### 1. Decide where the change belongs

| Change type | Destination |
|---|---|
| existing Chromium file | `cef/patch/patches/qnx/chromium/*.patch` |
| existing submodule file | `cef/patch/patches/qnx/*.patch` plus `patch.cfg` entry when required |
| brand-new source/config file | `cef/patch/qnx/chromium/new_files/...` |

### 2. Validate in the working tree first

- make the minimal root-tree change needed to fix the problem
- rebuild and rerun focused QNX tests before touching the durable patch set
- only after validation, refresh the CEF-managed patch or new-file copy

### 3. Refresh the durable artifact

- for patched existing files, update the corresponding patch file under `cef/patch/...`
- for new files, copy the finalized file into `cef/patch/qnx/chromium/new_files/...`
- for submodule patches, ensure the patch is relative to the submodule root and that `cef/patch/patch.cfg` points at the correct `path`
- generate CEF patches with `git diff --no-prefix --relative ...` so bootstrap can apply them consistently

### 4. Re-bootstrap from the durable artifacts

A change is not complete until it can be reproduced from the CEF-managed state:

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
./out/qnx_release/ninja_qnx.sh <target>
```

### 5. Re-run QEMU validation

At minimum:

```bash
sudo ./cef/tools/qnx_setup_env.sh
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"
```

### Practical rules

- do not leave a fix only in the root tree if it should be reproducible from bootstrap
- prefer extending an existing QNX patch when the change clearly belongs there
- keep patch scope narrow; avoid mixing unrelated fixes into one patch file unless they are coupled
- when a prior branch already solved the problem, port that fix into the CEF-managed patch set instead of inventing a new approach first

## Build commands

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
./out/qnx_release/ninja_qnx.sh base_unittests
```

For additional background and older experiments, see `history/`.
