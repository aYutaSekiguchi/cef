# QNX Port Working Notes

## Scope

This repository contains a QNX port of Chromium/CEF work centered on compatibility tag `147.0.7727.147`.

## Source of truth

- QNX patches: `cef/patch/patches/qnx/` and `cef/patch/patches/qnx/chromium/`
- QNX new files: `cef/patch/qnx/chromium/new_files/`
- QNX docs: `cef/docs/qnx/`
- Bootstrap script: `cef/tools/cef_create_projects_qnx.sh`

## Do

- keep QNX-specific changes under `cef/patch/...`
- keep new files under `cef/patch/qnx/chromium/new_files/...`
- use `cef/tools/cef_create_projects_qnx.sh` to regenerate a working QNX tree
- use `out/qnx_release/ninja_qnx.sh` for builds after bootstrap
- use `cef/tools/qnx_setup_env.sh` and `cef/tools/qnx_run_test.sh` for QEMU validation
- check `cef/docs/qnx/build-error-index.md` and `cef/docs/qnx/history/` before inventing a new fix

## Do not

- add unmanaged root-level `patches/` directories
- assume Linux runtime behavior just because GN treats QNX as Linux-like for some file selection
- rely on local tree edits as the durable source of truth when the change should be carried by a CEF-managed patch

## Standard commands

Bootstrap:

```bash
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
```

Build:

```bash
./out/qnx_release/ninja_qnx.sh base_unittests
```

QEMU setup:

```bash
sudo ./cef/tools/qnx_setup_env.sh
```

Broad test run:

```bash
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"
```

## Current validated exclusions

See `cef/docs/qnx/status.md` for the canonical exclusion list and rationale.

## Reference docs

- `cef/docs/qnx/status.md`
- `cef/docs/qnx/build-and-toolchain.md`
- `cef/docs/qnx/testing.md`
- `cef/docs/qnx/build-error-index.md`
- `cef/docs/qnx/history/`
