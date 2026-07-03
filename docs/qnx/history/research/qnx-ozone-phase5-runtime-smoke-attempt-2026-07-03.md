# QNX Ozone Phase 5 Runtime Smoke Attempt — 2026-07-03

## Scope

Attempt a first runtime smoke after Phase 5 compile scaffolding:

- temporary root tree with QNX Ozone `new_files` copied in
- `ozone_platform_qnx=true`
- build a small visual target (`ui/ozone/demo:ozone_demo`)
- run under QEMU virgl without using `--in-process-gpu`

This is **not** final out-of-process CEF acceptance. `ozone_demo` runs Ozone UI/GPU initialization in a single process and is only a bounded Screen/EGL startup probe for the new QNX Ozone platform.

## Build setup

Generated:

```bash
cd /home/yuta/chromium/src
git apply -p0 cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
git apply -p0 cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
mkdir -p ui/ozone/platform/qnx
cp -R cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/. ui/ozone/platform/qnx/
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_runtime \
    --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
```

Target discovery found:

- `cef:cefsimple`
- `cef:cefclient`
- `ui/ozone/demo:ozone_demo`

## Full CEF build attempt

A full `cef:cefsimple` build was started:

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_runtime cef:cefsimple -k 20 \
  2>&1 | tee out/qnx_phase5_runtime/cefsimple.build.log
```

It expanded to roughly `78833` build steps and was manually stopped around step `9731/78833` because it was too broad for the bounded Phase 5 smoke turn. No actionable compiler/linker failure was reached.

## Bounded visual target build

Built the smaller Ozone demo target:

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_runtime ui/ozone/demo:ozone_demo -k 20 \
  2>&1 | tee out/qnx_phase5_runtime/ozone_demo.build.log
```

Result:

- `6667/6667` targets completed
- `out/qnx_phase5_runtime/ozone_demo` exists

## Runtime checks

### `--help` sanity check

```bash
BUILD_DIR=/home/yuta/chromium/src/out/qnx_phase5_runtime \
  ./cef/tools/qnx_run.sh --kill-existing --timeout 30 -- \
  ./ozone_demo --help
```

Result:

- exits `0`
- prints `Usage:`
- confirms the executable can start and print help under QNX

### Headless comparison

```bash
BUILD_DIR=/home/yuta/chromium/src/out/qnx_phase5_runtime \
  ./cef/tools/qnx_run.sh --kill-existing --timeout 30 -- \
  ./ozone_demo --ozone-platform=headless --window-size=320x240
```

Result:

- no segmentation fault observed
- logs expected EGL initialization errors for headless in this environment
- command times out after 30s because `ozone_demo` keeps running

Relevant output:

```text
[ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
[ERROR:ui/ozone/common/gl_ozone_egl.cc:26] GLDisplayEGL::Initialize failed.
TimeoutError: QNX command timed out after 30.0 seconds
```

### QNX Ozone + virgl

```bash
BUILD_DIR=/home/yuta/chromium/src/out/qnx_phase5_runtime \
  ./cef/tools/qnx_run.sh --virgl --kill-existing --timeout 45 -- \
  ./ozone_demo --ozone-platform=qnx --window-size=320x240 \
  2>&1 | tee out/qnx_phase5_runtime/ozone_demo.virgl.log
```

Result: **failed**, exit `139`.

Failure signature:

```text
Received signal 11 si_addr=0x0
Context: rip=0x001f964a728a rsp=0x0032c3e7b770 rbp=0x000000000000
Symbol: /mnt/nfs/out/qnx_phase5_runtime/./ozone_demo _ZNSt3__216__pad_and_outputB7v160006IcNS_11char_traitsIcEEEENS_19ostreambuf_iteratorIT_T0_EES6_PKS4_S8_S8_RNS_8ios_baseES4_ + 0x0000000002ea
segmentation violation     (core dumped) sh -c './ozone_demo --ozone-platform=qnx --window-size=320x240'
__PI_QNX_EXIT__:139
```

Log:

- `out/qnx_phase5_runtime/ozone_demo.virgl.log`

No core file was found in `out/qnx_phase5_runtime` or the QEMU image directory after the run.

## Interpretation

- The crash is QNX-Ozone-specific in this bounded comparison: `--help` works, and `--ozone-platform=headless` does not segfault before timeout.
- The first symbol is in libc++ `std::__pad_and_output` with `si_addr=0x0`, which often indicates streaming a null C string through `std::ostream`/Chromium logging.
- The crash happens before useful QNX Ozone runtime logs are emitted in the serial output, so the next step should be a narrow startup-instrumentation/audit microtask, not broad CEF runtime work.
- This does **not** invalidate the compile-scaffold acceptance, but it blocks runtime smoke acceptance.

## Cleanup

After the attempt:

```bash
git checkout -- build/config/ozone.gni ui/ozone/BUILD.gn
rm -rf ui/ozone/platform/qnx
```

Root temporary GN/source state was cleaned. Build output directories/logs remain under `out/qnx_phase5_runtime/` for inspection.

## Next recommended action

Bounded runtime-breakage microtask:

1. Inspect QNX Ozone startup logging paths for raw nullable `const char*` streams.
2. Add minimal null-safe logging or startup breadcrumbs around `InitializeUI`, `QnxScreenContext`, `QnxWindow`, `QnxScreen`, `QnxSurfaceFactoryOzone`, and `QnxGLOzoneEGL`.
3. Rebuild only `ui/ozone/demo:ozone_demo`.
4. Re-run the same QEMU virgl command.
5. If the first crash is fixed, continue to the next runtime blocker; otherwise capture a better stack/signature.
