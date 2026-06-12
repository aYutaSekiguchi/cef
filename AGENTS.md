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
- for bootstrap-stage failures, hand off to the `qnx-bootstrap` skill (`cef/.agents/skills/qnx-cef-build/SKILL.md`) before debugging build/link/test issues
- use `out/qnx_release/ninja_qnx.sh` for builds after bootstrap
- use `cef/tools/qnx_setup_env.sh` and `cef/tools/qnx_run_test.sh` for QEMU validation
- run `cef/tools/qnx_sync_sources.sh -f -R` before `cef/tools/cef_create_projects_qnx.sh` when refreshing a QNX working tree
- create or refresh CEF-managed patch files with `git diff --no-prefix --relative --full-index` (or `cef/tools/patch_updater.py --resave`) from the correct patch root, and verify they apply cleanly on a clean tree before committing
- when bootstrap reports a failed patch, stop and repair the CEF-managed patch stack before continuing with GN/Ninja work; see `cef/docs/qnx/patch-hygiene.md`
- check `cef/docs/qnx/build-error-index.md` and `cef/docs/qnx/history/` before inventing a new fix

## Do not

- add unmanaged root-level `patches/` directories
- hand-edit patch headers or leave patch files with `a/` / `b/` prefixes, truncation, or mixed roots; regenerate them instead
- manually apply, skip, or partially delete patches to bypass a `cef_create_projects_qnx.sh` patch failure; stale patches must be fixed or removed in `cef/patch/` first
- hand-write generated GN state such as `out/qnx_release/args.gn` as a substitute for a successful bootstrap
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
- `cef/docs/qnx/patch-hygiene.md`
- `cef/docs/qnx/history/`
