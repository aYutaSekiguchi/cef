---
name: build-breakage-loop
description: Investigate and resolve repeated build, GN, compile, link, and test breakages during platform ports, Chromium upgrades, rebases, or downstream customization maintenance. Use this skill whenever work is blocked by one or more build errors and the agent should run the build, isolate the first actionable failure, search prior fix logs for similar patterns, apply a matching durable fix when justified, otherwise prepare options for the user, then record the resolved issue in a structured error catalog.
---

# Build Breakage Loop

## Overview

Use this skill to drive a repeatable loop for large breakage sets: reproduce one actionable failure, classify it, search only the relevant history, apply or propose a fix, verify it, and record the result so the next agent does less rediscovery.

Keep `SKILL.md` focused on the default operating procedure. Load `references/error-catalog.md` only when you need exact stage/category names, note layout, or scaffold details.

## Repository Rules

Respect the repository's durable-source-of-truth rules before touching code:

- Keep QNX-specific changes under `cef/patch/patches/qnx/` or `cef/patch/patches/qnx/chromium/`.
- Keep new QNX files under `cef/patch/qnx/chromium/new_files/`.
- Prefer `cef/tools/cef_create_projects_qnx.sh` to regenerate a QNX tree instead of relying on ad-hoc local edits.
- Use `out/qnx_release/ninja_qnx.sh` for builds after bootstrap.
- Check `docs/qnx/build-error-index.md`, `docs/qnx/history/`, and any structured notes under `docs/qnx/history/build-errors/` before inventing a new fix.
- Do not treat temporary local tree edits as durable if the final fix belongs in a managed patch.

## Default Posture

- **One blocker at a time:** Do not solve every error in the scrollback at once. Work the first actionable blocker, then loop.
- **Confidence gate:** Reuse a prior fix only when you are 100% confident the current failure matches the past case in root cause, affected code path, and fix shape. If confidence is lower, consult the user.
- **Subagent-first for bounded tasks:** If subagents are available, delegate discrete tasks such as reproducing a failure, summarizing diagnostics, searching prior notes, comparing platform guards, or prototyping one fix. Keep the main agent focused on context, loop control, and deciding whether to ask the user or proceed autonomously.
- **Protect the context window:** Build, GN, link, and test logs can be huge. Do not read long logs raw into the main context. Prefer token-efficient tooling such as context-mode (`ctx_execute`, `ctx_execute_file`) or shell filters (`grep`, `rg`, `head`, `tail`) so only the minimal failure signature enters context.

## Workflow

### 1. Reproduce one actionable failure

- Run the narrowest build or test command that reproduces the current blocker.
- Route long output through token-efficient summarization.
- Stop at the first actionable failure unless later lines clearly show the first line is only fallout.
- Capture a short failure signature:
  - failing target
  - file and line
  - primary diagnostic text
  - relevant symbol, header, macro, or syscall name

Common entrypoints in this repository:

```bash
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>
./out/qnx_release/ninja_qnx.sh <target>
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"
```

### 2. Classify before searching

Classify early so history lookups stay narrow.

Choose:

- `stage`: `bootstrap`, `gn`, `compile`, `link`, `package`, `test`
- `category`: one dominant cause class
- `scope`: subsystem or target, for example `base`, `build`, `content`, `net`, `third_party/googletest`

If classification is ambiguous, start with the earliest stage and the most concrete category, then refine later. Read `references/error-catalog.md` when you need the canonical taxonomy or note path scheme.

### 3. Search the smallest relevant history slice

Prefer targeted search over opening long files wholesale.

Search order:

1. `docs/qnx/build-error-index.md`
2. `docs/qnx/history/`
3. `docs/qnx/history/build-errors/<stage>/<category>/`

Search by exact symbol, error code, file name, subsystem, API, or macro first. Read only the few most relevant notes.

### 4. Decide whether to apply or escalate

#### Apply a fix directly only when all are true

- You are 100% confident the current failure matches the prior case.
- A prior note shows the same failure pattern or root-cause family.
- The local code path matches closely enough that the same fix pattern is defensible.
- The intended durable location is clear.
- The change is reversible and easy to verify.

Transfer the pattern, not the old patch text.

#### Investigate further when no note matches cleanly

Prefer bounded autonomous investigation, ideally via subagents:

- inspect nearby code
- inspect build files and platform guards
- compare with adjacent platforms if useful
- add temporary probes only when necessary

Remove temporary instrumentation before presenting options if the final fix is still uncertain.

#### Return to the user instead of guessing when any are true

- confidence is below 100% that a past case truly matches
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

For each resolved or well-understood blocker:

- Create or update a note under `docs/qnx/history/build-errors/`.
- Read `references/error-catalog.md` when choosing stage/category names, note path, or scaffold usage.
- Keep `docs/qnx/build-error-index.md` short and search-oriented.
- Do not paste full build logs. Keep only the minimal lines needed to identify the pattern.

## Reference

- `references/error-catalog.md` — canonical stage/category names, note path scheme, note template, search heuristics, and scaffold usage.
