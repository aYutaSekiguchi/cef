# ipcz reference driver needs a QNX ShmMemory backend

- Date: 2026-06-09
- Signature: `'kFileDescriptor' in 'ipcz::reference_drivers::Object'` / `MFD_ALLOW_SEALING` / `F_SEAL_SHRINK`
- Stage: compile
- Category: platform-api-gap
- Scope: `third_party/ipcz/src/reference_drivers`

## Symptoms

After the variations / ipcz random fixes, the `mojo:mojo_unittests` build moved into `ipcz/reference_drivers/` and produced:

```text
wrapped_file_descriptor.h:17:56: error: no member named 'kFileDescriptor' in 'ipcz::reference_drivers::Object'
wrapped_file_descriptor.h:25:12: error: use of undeclared identifier 'ReleaseAsHandle'; did you mean 'Object::ReleaseAsHandle'?
wrapped_file_descriptor.h:30:12: error: use of undeclared identifier 'TakeFromHandle'
memfd_memory.cc:51:38: error: use of undeclared identifier 'MFD_ALLOW_SEALING'
memfd_memory.cc:57:22: error: use of undeclared identifier 'F_ADD_SEALS'
multiprocess_reference_driver.cc:72-101: no member named 'kFileDescriptor' / 'TakeFromHandle' / 'TakeDescriptor'
```

## QNX investigation findings

- QNX 8 sysroot exposes `shm_open` / `shm_unlink` / `mmap` / `MAP_SHARED` in `<sys/mman.h>`.
- `SHM_ANON` is a QNX-specific flag for an anonymous shm object, see `<sys/mman.h>` in the SDP 8.0 sysroot.
- QNX 8 sysroot does **not** expose `memfd_create` / `MFD_ALLOW_SEALING` / `F_SEAL_*` / `F_ADD_SEALS` in the public `<fcntl.h>` or `<sys/mman.h>` headers; only `devs/sys/mman.h` documents them as planning notes. Therefore the Linux `MemfdMemory` design cannot be reused.
- `qnx-ports/libsysv-ipc-shim` is a SysV-IPC shim (`shmget`/`shmat`), unrelated to POSIX `shm_open` / `memfd_create`.
- `qnx-ports/build-files` and the `qnx-ports` org contain no Chromium/Mojo/ipcz ports.

## Design

Introduce a QNX-native shm backend that the future multiprocess reference driver can layer on top of:

1. Extend `Object::Type` with `kQnxShmHandle` (QNX counterpart of the Linux-only `kFileDescriptor`).
2. Add `reference_drivers/qnx/qnx_shm_memory.{h,cc}`: anonymous POSIX shm via `shm_open(SHM_ANON)` + `ftruncate` + `mmap`, mirroring `MemfdMemory` API surface.
3. Add `reference_drivers/qnx/qnx_shm_handle.h`: a no-op placeholder that documents why the QNX `WrappedFileDescriptor` does not need a separate handle Object layer.
4. Header-only specialize `WrappedFileDescriptor` for QNX so it reports `Object::kQnxShmHandle` while storing its `FileDescriptor` directly. Keep the existing `.cc` as a near no-op so the existing source list in `third_party/ipcz/src/BUILD.gn` keeps compiling.
5. Wire the new directory into the ipcz `reference_drivers` target via a small `qnx/BUILD.gn`.

This is the foundation; the follow-up task ports `multiprocess_reference_driver.cc` (and `memfd_memory.cc`) from `MemfdMemory` to `QnxShmMemory`. After that, `Object::kQnxShmHandle` is the natural slot for downstream serialization hooks (e.g. emitting `shm_open_handle` tokens).

## Applied change

- `third_party/ipcz/src/reference_drivers/object.h` adds `kQnxShmHandle` under `OS_QNX`.
- `third_party/ipcz/src/reference_drivers/wrapped_file_descriptor.{h,cc}` switch the QNX variant to `kQnxShmHandle` (header-only body).
- New managed files:
  - `cef/patch/qnx/chromium/new_files/third_party/ipcz/src/reference_drivers/qnx/qnx_shm_handle.h`
  - `cef/patch/qnx/chromium/new_files/third_party/ipcz/src/reference_drivers/qnx/qnx_shm_memory.{h,cc}`
  - `cef/patch/qnx/chromium/new_files/third_party/ipcz/src/reference_drivers/qnx/BUILD.gn`

## Verification

- `git apply --check` succeeds for both `ipcz_qnx_shm_backend.patch` and `ipcz_memfd_and_multiprocess_qnx.patch`.
- A clean-tree bootstrap (after `ipcz_random_qnx_dev_urandom` and `variations_study_qnx_platform`) re-runs `./out/qnx_release/ninja_qnx.sh -C out/qnx_release mojo:mojo_unittests` and the next blocker shifts from the `kFileDescriptor` / `MFD_ALLOW_SEALING` / `TakeFromHandle` family to the next gap, currently the **Mojo C bindings generator** hitting QNX sysroot headers without the `--target=x86_64-unknown-nto` flag:
  ```
  gen/mojo/public/rust/system/mojo_c_system_bindings_generator/bindings.rs
  /home/yuta/qnx800/target/qnx/usr/include/sys/compiler_gnu.h:60:3: error: Endian not defined
  /home/yuta/qnx800/target/qnx/usr/include/sys/platform.h:428:3: error: not configured for target
  /home/yuta/qnx800/target/qnx/usr/include/stdint.h:70:9: error: unknown type name '_Intleast8t'
  ```
- The QNX ShmMemory driver itself compiles and links into `reference_drivers`, ready to be exercised by the follow-up multiprocess port.

## Files touched

- `cef/patch/patches/qnx/chromium/ipcz_qnx_shm_backend.patch`
- `cef/patch/patches/qnx/chromium/ipcz_memfd_and_multiprocess_qnx.patch`
- `cef/patch/qnx/chromium/new_files/third_party/ipcz/src/reference_drivers/qnx/{qnx_shm_handle.h,qnx_shm_memory.h,qnx_shm_memory.cc,BUILD.gn}`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/platform-api-gap/qnx-ipcz-qnx-shm-backend.md`
- `cef/docs/qnx/build-error-index.md`
