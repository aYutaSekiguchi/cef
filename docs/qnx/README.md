# QNX Port Documentation

This directory contains the working documentation for the Chromium/CEF QNX port.

## Recommended reading order

1. `status.md` — current validated state and what to run on another machine
2. `build-and-toolchain.md` — bootstrap flow, toolchain model, GN args, constraints
3. `testing.md` — QEMU/NFS setup and test execution workflow
4. `fixes-and-decisions.md` — detailed fix log and rationale

## Main documents

| File | Purpose |
|---|---|
| `status.md` | Current port status, validated baseline, and cross-machine checklist |
| `build-and-toolchain.md` | Build bootstrap, toolchain design, libc++ strategy, and constraints |
| `testing.md` | QEMU runner usage, manual boot flow, and result inspection |
| `fixes-and-decisions.md` | Detailed fix history and decision log |
| `history/` | Older investigations, status snapshots, reviews, and handoff material |

## Current baseline

- Chromium compatibility tag: `147.0.7727.147`
- QNX SDP root: local SDK installation path (`<QNX_SDP_ROOT>`)
- Default build directory: `out/qnx_release`
- Bootstrap entrypoint: `cef/tools/cef_create_projects_qnx.sh`
- QEMU helpers: `cef/tools/qnx_setup_env.sh`, `cef/tools/qnx_run.sh`, `cef/tools/qnx_run_test.sh`

## Placeholders used in this documentation

| Placeholder | Meaning |
|---|---|
| `<CHROMIUM_SRC_ROOT>` | local Chromium `src` checkout root |
| `<QNX_SDP_ROOT>` | local QNX SDP installation root |
| `<QNX_QEMU_DIR>` | local QEMU image directory used for QNX boot/testing |

## Patch layout

All QNX-specific changes are managed under `cef/`:

- `cef/patch/patches/qnx/chromium/` — Chromium patches
- `cef/patch/patches/qnx/` — submodule patches
- `cef/patch/qnx/chromium/new_files/` — new files added by the port

Do not introduce unmanaged root-level `patches/` directories.
