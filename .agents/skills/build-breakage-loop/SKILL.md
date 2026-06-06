---
name: build-breakage-loop
description: Investigate and resolve repeated build, GN, compile, link, and test breakages during platform ports, Chromium upgrades, rebases, or downstream customization maintenance. Use this skill whenever work is blocked by one or more build errors and the agent should run the build, isolate the first actionable failure, search prior fix logs for similar patterns, apply a matching durable fix when justified, otherwise prepare options for the user, then record the resolved issue in a structured error catalog.
---

# Build Breakage Loop

## Overview

Use this skill to drive a repeatable fix loop for large breakage sets: reproduce one actionable failure, classify it, search only the relevant history, apply or propose a fix, verify it, and record the result so the next agent does less rediscovery.

This skill is intentionally broader than a single port. It should work for QNX bring-up, Chromium version bumps, rebases against upstream, and downstream maintenance where the same classes of failures reappear under slightly different symptoms.

## Repository Rules

Respect the repository's durable-source-of-truth rules before touching code:

- Keep QNX-specific changes under `cef/patch/patches/qnx/` or `cef/patch/patches/qnx/chromium/`.
- Keep new QNX files under `cef/patch/qnx/chromium/new_files/`.
- Prefer `cef/tools/cef_create_projects_qnx.sh` to regenerate a QNX tree instead of relying on ad-hoc local edits.
- Use `out/qnx_release/ninja_qnx.sh` for builds after bootstrap.
- Check `docs/qnx/build-error-index.md`, `docs/qnx/history/`, and any structured notes under `docs/qnx/history/build-errors/` before inventing a new fix.
- Do not treat temporary local tree edits as durable if the final fix belongs in a managed patch.

## Workflow

### 1. Reproduce one actionable failure

Do not try to solve every error in the scrollback at once.

- Run the narrowest build or test command that reproduces the current blocker.
- Stop at the first actionable failure unless later lines clearly show the first line is only fallout.
- Capture a short failure signature:
  - failing target
  - file and line
  - primary diagnostic text
  - relevant symbol, header, macro, or syscall name

For this repository, common entrypoints are:

```bash
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
./out/qnx_release/ninja_qnx.sh <target>
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"
```

### 2. Classify before searching

Classify the failure early so history lookups stay narrow. Use the taxonomy in `references/error-catalog.md`.

Choose:

- `stage`: `bootstrap`, `gn`, `compile`, `link`, `package`, `test`
- `category`: one dominant cause class such as `platform-api-gap`, `upstream-api-drift`, `missing-include`, `symbol-visibility`, `build-graph`, `test-environment`, or `runtime-assumption`
- `scope`: subsystem or target, for example `base`, `build`, `content`, `net`, `third_party/googletest`

If classification is ambiguous, start with the earliest stage and the most concrete category. Refine later if the investigation proves otherwise.

### 3. Search the smallest relevant history slice

Prefer targeted search over opening long files wholesale.

Search order:

1. Search `docs/qnx/build-error-index.md` with `rg` using the failure signature.
2. Search `docs/qnx/history/` for subsystem names, API names, error codes, and prior investigations.
3. If the structured catalog exists, search only the matching branch under `docs/qnx/history/build-errors/<stage>/<category>/`.

Useful patterns:

```bash
rg -n "posix_spawnp|EBADF|launch_qnx" docs/qnx
rg -n "TakeError::kUnexpectedReadOnlyFd|F_GETFL" docs/qnx
rg -n "header-name|missing symbol|type name" docs/qnx/history
```

Read only the few most relevant notes. Do not load unrelated archives just because they are nearby.

### 4. Decide whether to apply or escalate

#### Apply a fix directly when all of these are true

- A prior note shows the same failure pattern or the same root-cause family.
- The local code path matches the prior conditions closely enough that the same fix pattern is defensible.
- The intended durable location is clear.
- The change is reversible and easy to verify with a rebuild.

When reusing a prior fix, transfer the pattern, not the text. Confirm the surrounding code and current upstream version still justify it.

#### Investigate further when there is no matching note

Perform autonomous investigation:

- inspect nearby code
- inspect build files and platform guards
- compare with adjacent platforms if useful
- add temporary probes only when necessary

Temporary instrumentation is allowed during investigation, but remove it before presenting options to the user if the final fix is still uncertain.

#### Return to the user instead of guessing when any of these are true

- multiple plausible fixes would change semantics differently
- the fix needs product or platform policy input
- the safest path is to exclude, skip, or weaken behavior and the tradeoff is non-trivial
- you cannot tell whether the change belongs in a managed patch, local source, or test-only workaround

When escalating, present:

1. the short failure signature
2. the most likely root cause
3. 2-3 concrete fix options with tradeoffs
4. any temporary investigation edits already removed

### 5. Verify and loop

- Re-run the narrow reproducer first.
- If the blocker is gone, continue to the next actionable failure.
- If the fix changed behavior more broadly, run the next wider build or test layer.
- Keep the loop focused on one newly exposed blocker at a time.

### 6. Record the resolution

Every resolved blocker should leave a structured note so later agents can search by stage and cause class instead of rereading one long diary.

- Create or update a note under `docs/qnx/history/build-errors/`.
- Use `scripts/scaffold_error_note.py` to create a normalized file path and template.
- Keep `docs/qnx/build-error-index.md` short and search-oriented, and store detailed reusable cases in the structured tree.

## Structured Log Layout

Use this path scheme for resolved or well-understood breakages:

```text
docs/qnx/history/build-errors/<stage>/<category>/<slug>.md
```

Examples:

```text
docs/qnx/history/build-errors/compile/platform-api-gap/qnx-fcntl-getfl-shm-fd.md
docs/qnx/history/build-errors/test/test-environment/qemu-nfs-rename-semantics.md
docs/qnx/history/build-errors/link/symbol-visibility/allocator-shim-page-allocator.md
```

This layout keeps retrieval local:

- stage narrows where in the pipeline the failure appears
- category narrows likely cause
- slug preserves the concrete incident

Read `references/error-catalog.md` when choosing stage or category names.

## Recording Standard

Each note should be concise but complete enough for reuse. Include:

- `Date`
- `Signature`
- `Symptoms`
- `Classification`
- `Root cause`
- `Fix pattern`
- `Applied change`
- `Verification`
- `Files touched`
- `Related notes`

Do not paste entire build logs. Keep only the minimal lines needed to identify the pattern.

## Using The Scaffold Script

From the repository root:

```bash
python .agents/skills/build-breakage-loop/scripts/scaffold_error_note.py \
  --stage compile \
  --category platform-api-gap \
  --title "QNX fcntl F_GETFL shared memory descriptors report O_RDONLY" \
  --signature "Unexpected(4) = TakeError::kUnexpectedReadOnlyFd"
```

The script creates a note path under `docs/qnx/history/build-errors/` and fills in the standard headings.

## References

- Read `references/error-catalog.md` for the classification scheme, storage layout, and search heuristics.
