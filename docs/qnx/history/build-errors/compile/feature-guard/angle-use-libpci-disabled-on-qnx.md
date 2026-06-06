# ANGLE must not enable Linux libpci probing on QNX

- Date: 2026-06-03
- Signature: no member named 'pci_alloc' in the global namespace
- Stage: compile
- Category: feature-guard
- Scope: ANGLE gpu_info_util

## Symptoms

- `SystemInfo_libpci.cpp` failed with many missing symbol and type errors, including `pci_alloc`, `pci_init`, `pci_dev`, and `PCI_REVISION_ID`.
- The failure happened while building ANGLE's GPU info utility.

## Root cause

- `use_libpci` was enabled through Linux-like GN conditions because `is_linux` is true for QNX in Chromium's build configuration.
- QNX ships a library named `libpci`, but its API is a QNX-native PCI server interface, not the Linux libpci API ANGLE expects.
- As a result, the failure occurred at compile time, not link time.

## Fix pattern

- Do not assume a similarly named library on QNX is source-compatible with the Linux version.
- If the Linux fallback is not needed for the current product goal, disable the feature at GN-arg level instead of carrying a fake compatibility layer.

## Applied change

- Added `use_libpci = false` to the QNX GN args emitted by `cef_create_projects_qnx.sh`.
- Let ANGLE keep using its existing non-libpci fallback paths.

## Verification

- The `SystemInfo_libpci.o` failure disappeared.
- The build proceeded to the next target.

## Files touched

- `cef/tools/cef_create_projects_qnx.sh`
- `out/qnx_release/args.gn`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/build-graph/cpuinfo-qnx-fork-path-switch.md`
