---
name: qnx-bootstrap
description: Diagnose and fix QNX CEF (Chromium Embedded Framework) bootstrap failures for the QNX 800 port of Chromium 147.0.7727.147. Use this skill whenever the user reports `cef_create_projects_qnx.sh` failing with "patches failed to apply", a non-zero `git apply` exit, a `gclient sync` mismatch on QNX sources (`farmhash_qnx`, `epoll`, `cpuinfo_qnx`), an `args.gn` regeneration problem, a CEF rebase conflict on `origin/qnx_7727`, or any error message that contains "QNX", "qnx800", "out/qnx_release", or the path `cef/patch/patches/qnx/`. Also trigger when the user mentions "clean tree", "fresh apply", "idempotent bootstrap", or "revert CEF patches". This skill is the QNX bootstrap-specific companion to the general `build-breakage-loop` skill; for compile/link/test failures past the `gn gen` step, defer to that skill instead.
---

# QNX CEF Bootstrap

A QNX-port-specific recovery workflow for `cef_create_projects_qnx.sh`. Scope is strictly the bootstrap pipeline (env setup → patch application → args.gn generation → `gn gen`). For compile / link / test failures that surface *after* `gn gen` succeeded, defer to the general `build-breakage-loop` skill in the same directory.

## When this skill triggers

- "bootstrap failed" / `patches failed to apply` from `cef_create_projects_qnx.sh`
- `git apply` non-zero exit in the bootstrap log
- `gclient sync` warning about `farmhash_qnx/src`, `epoll/src`, `cpuinfo_qnx/src`
- `args.gn` regeneration problem (Dawn/build-graph flags missing or wrong)
- `git rebase origin/qnx_7727` conflict inside `cef/`
- Any mention of "QNX bootstrap" / "clean tree" / "idempotent bootstrap" / "revert CEF patches"

## The clean-tree-to-bootstrap recipe

This is the durable workflow as of 2026-06-07. Run **all four steps in order**. Do not skip `gclient sync` — without it QNX-specific submodules (`third_party/farmhash_qnx`, `third_party/epoll`, `third_party/cpuinfo_qnx`) are absent and downstream patches fail to find their targets.

```bash
cd <CHROMIUM_SRC>
git checkout -f                    # parent repo tracked files → HEAD
gclient sync -f -R                 # force submodule reset to DEPS.lock
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
./out/qnx_release/ninja_qnx.sh base_unittests
```

The first three steps together take ~30 s on this machine. Bootstrap itself is normally 25-35 s.

`<CHROMIUM_SRC>` means the Chromium checkout root on the current machine (the directory that contains `cef/`).

### gclient sync WARNINGs: what they mean and what to do

`gclient sync -f -R` may emit lines like:

```
WARNING: 'src/third_party/farmhash_qnx/src' is no longer part of this client.
WARNING: 'src/third_party/epoll/src' is no longer part of this client.
WARNING: 'src/third_party/cpuinfo_qnx/src' is no longer part of this client.
```

These three are **DEPS-managed QNX ports** (`qnx-ports/farmhash`, `qnx-ports/epoll`, `qnx-ports/cpuinfo`) registered by the `qnx_source_sync` patch. They are not in the default Chromium `DEPS` set; the patch is what makes gclient know about them. The WARNING fires when their target directories already exist on disk and the gclient-managed set does not include their commit hash.

**Do not** respond to this WARNING by running `gclient sync -D` — that would also delete the now-absent subdirs and any local state inside them. The WARNING is informational.

If the directories are missing entirely (so downstream patches under `qnx/chromium/farmhash_qnx_paths`, `qnx/chromium/cpuinfo_qnx_paths`, `qnx/chromium/base_build_sources_qnx` fail to find their targets), re-run `cef/tools/qnx_sync_sources.sh` to re-create them. The `qnx_source_sync` patch then makes gclient pick them up on the next sync.

In the normal clean-tree recipe, the directories survive `gclient sync -f -R` and the WARNING is a no-op.

## Why a second bootstrap aborts (and the workaround)

`cef_create_projects_qnx.sh` is **not idempotent across re-runs**. After a successful bootstrap, the second invocation reaches `168 patches total (0 applied, 166 skipped, 2 failed)` and aborts. The two typical failures are:

- `content_main_654986` (CEF core patch)
- `qnx/chromium/build_qnx_toolchain`

Root cause: a small number of CEF core / QNX patches contain hunks that `git apply --reverse --check` cannot detect as already-applied when the surrounding tree has shifted (CEF 147 rebase collisions). They fall through to `patch -p0 --force` which then aborts on a re-apply.

**The only reliable recovery is the clean-tree recipe above.** Do not attempt to fix this with `patch_updater.py --revert` — it is a `git checkout -- file` wrapper that only restores files whose contents exactly match HEAD, and the fuzzy-match-applied hunks above no longer do.

If you only need to revert a single named patch between bootstraps:

```bash
python3 cef/tools/patch_updater.py --revert --patch <patch-name>
```

This works for the 166/168 patches that are not rebase-collisions. It does not work for the 2 rebase-collision patches.

## Known CEF 147 rebase collisions (3 patches)

These patches require exact-match context that Chromium 147 broke. Until they are re-rebased upstream, expect them to need fuzzy match on the first apply and to fail on the second. They are safe to ignore on a fresh tree.

| Patch | Symptom if reverse-detected | Workaround |
|---|---|---|
| `qnx/chromium/build_qnx_toolchain` | `error: patch failed: build/config/BUILD.gn:209` | first-apply succeeds via `patch -p0 --force` fuzzy; subsequent runs need clean tree |
| `qnx/chromium/process_thread_qnx` | `error: patch failed: base/process/process_iterator.h:115` (27 hunks fail) | same |
| `qnx/chromium/angle_qnx_minimal_linux_headless` | ANGLE / GL-related rebase collision | same |

## Subtle traps in the CEF patcher

1. **`b/` prefix in patch paths**: `patch_updater.py --revert` outputs messages like `Skipping non-existing file <CHROMIUM_SRC>/b/third_party/angle/BUILD.gn`. The `b/` comes from `git apply` style patches and is **expected** — the file does not actually exist at that path, so the message is informational, not an error. Ignore it.

2. **CWD-sensitive git operations**: `patch_updater.py` runs `git checkout -- file` from the chromium parent repo CWD (`<CHROMIUM_SRC>`). If a CEF core patch deletes a CEF API header (e.g. `chrome_browser_context_menus.patch` deletes `include/base/cef_build.h` and `include/internal/cef_types.h`), the parent-repo CWD cannot find that path. The revert then runs `os.remove` on it, **which is what is supposed to happen** — the deletion is the patch's purpose. Subsequent bootstraps re-delete the header as part of the apply, so the cycle is consistent.

3. **`cef/` is an independent git repo**. It is not a submodule. It has its own HEAD, its own `origin/qnx_7727`, and its own `tools/`. Do not run `git rebase` from the chromium parent repo CWD when you mean to rebase the CEF branch.

4. **`gclient sync -f -R` resets submodules to DEPS.lock**. It does **not** touch `cef/` (because `cef/` is not a submodule). It does reset everything under `third_party/`, which is what you want.

## CEF rebase workflow

When the user asks to rebase `cef/` against `origin/qnx_7727`:

```bash
cd <CHROMIUM_SRC>/cef
git fetch origin
git rebase origin/qnx_7727
```

If the local and origin branches diverged via a force-push of the same logical commit, you will see `add/add` conflicts on the files that commit touched (e.g. `docs/qnx/history/build-errors/gn/build-graph/qnx-use-dawn-false-does-not-remove-dawn-...md` and `patch/patches/qnx/chromium/enable_on_device_model_qnx.patch`).

Resolution recipe (verified 2026-06-07):

```bash
# Verify the two commits have identical file content before assuming
# --theirs is safe. If md5sums differ, this is a real merge, not a
# force-push duplicate.
for f in <conflicting-file-1> <conflicting-file-2>; do
  printf "  HEAD:    %s\n" "$(git show HEAD:$f | md5sum | awk '{print $1}')"
  printf "  origin:  %s\n" "$(git show origin/qnx_7727:$f | md5sum | awk '{print $1}')"
done

# If md5sums match, --theirs is safe:
git checkout --theirs <conflicting-file-1> <conflicting-file-2>
git add <conflicting-file-1> <conflicting-file-2>
GIT_EDITOR=true git rebase --continue
```

`GIT_EDITOR=true` is required so the rebase does not block on an editor for the "stop for amend" prompt.

## args.gn regeneration

`args.gn` lives at `out/qnx_release/args.gn` and is regenerated by bootstrap Phase 4 (`cat <<EOF > "${GN_ARGS_FILE}"`). It is **not** tracked by either git repo. Two flags that have to be there for the build to compile, as of 2026-06-07:

- `use_dawn = false`
- `dawn_enable_vulkan = false` (added in `344d4f133` / `52e41334d`, see `compile/build-graph/dawn-disabled-for-headless-qnx.md`)
- `dawn_use_swiftshader = false` (required because `dawn_use_swiftshader ⇒ dawn_enable_vulkan` is asserted in `dawn/native/BUILD.gn:45`)

If a flag is missing or wrong, do not edit `args.gn` by hand — re-run bootstrap so Phase 4 regenerates from the canonical template.

## Recording a new bootstrap-stage breakage

When a new failure is understood well enough to reuse later, write a structured note under:

```
docs/qnx/history/build-errors/bootstrap/<category>/<slug>.md
```

Use the template and helpers from the `build-breakage-loop` skill. Categories that have been used in this tree: `build-graph` (patch cfg / `--revert` semantics), `toolchain-config` (sysroot, QNX_HOST/QNX_TARGET), `runtime-assumption` (submodule sync).

Always link the new note from `docs/qnx/build-error-index.md` via a one-line entry under the matching search hint, not a long narrative — that file is the entry point, not a diary.

## Cross-references

- `cef/AGENTS.md` — durable-source-of-truth rules, do/don't, standard commands
- `cef/docs/qnx/build-error-index.md` — entry point for looking up prior failures
- `cef/docs/qnx/status.md` — current validated baseline + accepted exclusions
- `cef/docs/qnx/build-and-toolchain.md` — bootstrap and toolchain behaviour
- `cef/docs/qnx/history/build-errors/` — structured per-incident notes
- `cef/.agents/skills/build-breakage-loop/` — general loop for compile/link/test failures past `gn gen`
- `cef/patch/patch.cfg` — every patch this bootstrap can apply, in apply order
- `cef/tools/cef_create_projects_qnx.sh` — the bootstrap script itself (Phase 1 → 5)
- `cef/tools/patch_updater.py` — `--revert` and `--resave` semantics
- `cef/tools/patcher.py` — the per-patch applier with `--patch-file` and `--patch-dir`

## Verification before declaring success

Always run the build before reporting a bootstrap fix as done. A bootstrap that exits 0 is necessary but not sufficient — `gn gen` may have produced a build graph that later fails to link.

```bash
./out/qnx_release/ninja_qnx.sh base_unittests
```

Acceptable stopping point: ninja reaches `base_unittests` link or the first compile failure past `[1/6xxxx]`. Report the FAILED count and the first actionable signature.
