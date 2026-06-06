# V8 snapshot host tools must stay keyed to host OS, not QNX target OS

- Date: 2026-05-30
- Signature: clang_x64 host tools compile platform-qnx.cc or hit invalid V8_TARGET_OS_LINUX expressions
- Stage: compile
- Category: feature-guard
- Scope: v8 host tools

## Symptoms

- Clean QNX builds failed while compiling host-side V8 tools such as `mksnapshot`.
- Host `clang_x64` started compiling QNX-only sources like `platform-qnx.cc`.
- Earlier failure modes also included invalid preprocessor expressions in `std-object-sizes.h` when `V8_HAVE_TARGET_OS` was unset.

## Root cause

- V8 needs two separate OS concepts during snapshot builds: the host/runtime OS of the tool being built and the target snapshot OS.
- The QNX port initially treated `target_os == "qnx"` as the selector for platform sources, which was wrong for host tools still running on Linux.
- Upstream V8 also lacked `V8_TARGET_OS_QNX`, making the target/runtime split incomplete.

## Fix pattern

- Keep target-OS macros and runtime host-OS source selection separate.
- Add missing target-OS support explicitly rather than overloading existing Linux selectors.
- For host-only compile-time checks, key conditions to runtime host macros such as `V8_OS_LINUX`, not target macros.

## Applied change

- Added `V8_TARGET_OS_QNX` support in `v8/include/v8config.h` and `v8/BUILD.gn`.
- Kept target define injection keyed to `target_os == "qnx"`.
- Kept platform and trap-handler source selection keyed to host/runtime OS.
- Changed the Linux-only object-size guard in `std-object-sizes.h` from `V8_TARGET_OS_LINUX` to `V8_OS_LINUX`.
- Excluded QNX from V8 trap-handler POSIX source selection.

## Verification

- Host `clang_x64` V8 tools such as `mksnapshot`, `v8_context_snapshot_generator`, `mkgrokdump`, and `v8_shell` built correctly again.
- Clean QNX bootstrap/build no longer depended on extra local V8 edits.

## Files touched

- `v8/BUILD.gn`
- `v8/include/v8config.h`
- `v8/src/wasm/std-object-sizes.h`
- `cef/patch/patches/qnx/chromium/v8_qnx_targeting.patch`

## Related notes

- `docs/qnx/build-error-index.md`
