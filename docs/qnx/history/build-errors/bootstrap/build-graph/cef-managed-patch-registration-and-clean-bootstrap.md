# Clean bootstrap must register all validated QNX fixes in the CEF-managed patch flow

- Date: 2026-05-30
- Signature: clean bootstrap misses validated QNX fixes or corrupt patch hunk blocks patch application
- Stage: bootstrap
- Category: build-graph
- Scope: CEF patch registration and durable source of truth

## Symptoms

- A clean-tree validation failed even though the live working tree already built successfully.
- `partition_alloc_qnx.patch` failed to apply because of a corrupt hunk header.
- Perfetto and V8 fixes existed in local working-tree form but were not yet fully preserved in the CEF-managed patch flow.
- Rebuild logs were flooded with repeated `std::atomic_ref` CTAD warnings.

## Root cause

- Some validated fixes had not been captured in the durable CEF patch or new-file sources of truth.
- The patch registration path and the developer's live working tree diverged.
- One patch file also contained a malformed hunk header, so even the intended durable source could not replay cleanly.

## Fix pattern

- Treat a clean bootstrap from patch sources as the real verification step, not just a build from an already-modified tree.
- As soon as a fix is validated locally, move it into `cef/patch/...`, `cef/patch/qnx/.../new_files`, and bootstrap registration.
- Repair broken patch metadata before investigating higher-level build symptoms.

## Applied change

- Corrected the broken hunk header in `partition_alloc_qnx.patch`.
- Registered the Perfetto ELF macro-collision fix in `patch/patch.cfg`.
- Captured V8 work in durable patch files: `v8_qnx_targeting.patch` and `v8_base64_atomic.patch`.
- Added the V8 patches to phase 3 application in `cef/tools/cef_create_projects_qnx.sh`.
- Added an `atomic_ref(T&) -> atomic_ref<T>` deduction guide to `qnx_std_polyfill.h`.

## Verification

- Clean bootstrap/build verification succeeded from CEF-managed patch sources.
- Rebuild logs stopped emitting the repeated `std::atomic_ref` CTAD warning flood.

## Files touched

- `cef/patch/patches/qnx/chromium/partition_alloc_qnx.patch`
- `cef/patch/patches/qnx/perfetto_qnx_elf.patch`
- `cef/patch/patches/qnx/chromium/v8_qnx_targeting.patch`
- `cef/patch/patches/qnx/chromium/v8_base64_atomic.patch`
- `cef/patch/qnx/chromium/new_files/build/config/qnx/qnx_std_polyfill.h`
- `cef/tools/cef_create_projects_qnx.sh`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/build-error-index.md`
