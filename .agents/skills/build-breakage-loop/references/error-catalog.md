# Error Catalog

Use this reference to keep build-breakage notes searchable without forcing future agents to read one growing log file.

Load this file only when you need one of the following:

- canonical `stage` or `category` names
- the structured note path scheme
- the note template for a resolved blocker
- the scaffold command for creating a new note

## Goals

- Separate retrieval by pipeline stage first, because the debugging surface changes a lot between bootstrap, compile, link, and test failures.
- Separate retrieval by dominant cause class second, because many seemingly different diagnostics collapse to the same fix pattern.
- Keep each note small and local so `rg` can find the right branch quickly.

## Storage Layout

Store detailed notes under:

```text
docs/qnx/history/build-errors/<stage>/<category>/<slug>.md
```

Use `docs/qnx/build-error-index.md` as the lightweight entry point, and treat the structured tree as the retrieval-oriented index.

## Stage Names

Use one of these stable stage names:

- `bootstrap`: environment setup, SDK detection, sysroot, script prerequisites
- `gn`: `gn gen`, args expansion, toolchain config, target graph construction
- `compile`: C, C++, Objective-C, Rust, generated source compilation
- `link`: archive/shared library/executable link failures, missing symbols, duplicate symbols
- `package`: packaging, resource staging, install image assembly
- `test`: launcher failures, runtime assumptions, flaky environment limits, QEMU/NFS-specific behavior

Choose the earliest stage where the issue becomes actionable.

## Category Names

Use one dominant category. Add cross-references inside the note rather than multiple directories.

- `platform-api-gap`: OS or libc behavior differs from Linux assumptions
- `upstream-api-drift`: Chromium upgrade changed signatures, types, ownership, or contracts
- `missing-include`: include order or missing header exposes hidden dependency
- `type-trait-template`: template deduction, requires-clause, type-trait, or specialization mismatch
- `build-graph`: GN target wiring, source selection, deps, visibility, generated files
- `symbol-visibility`: missing symbol, duplicate symbol, export/import, archive ordering
- `feature-guard`: wrong or missing `BUILDFLAG`, `#if`, target enablement, or platform exclusion
- `toolchain-config`: compiler flags, sysroot, libc++, linker, assembler, or SDK path mismatch
- `test-environment`: QEMU, NFS, timeout, machine capacity, hermeticity, or launcher behavior
- `runtime-assumption`: test or helper assumes filesystem, procfs, memory, spawn, or fd semantics that do not hold

If a note does not fit any category, prefer adding one new category with a clear definition rather than overloading an existing one.

## Slug Style

Use a short slug that combines the concrete subsystem and the core symptom:

- `qnx-fcntl-getfl-shm-fd`
- `launch-qnx-posix-spawn-ebadf`
- `googletest-death-test-cwd-fd`

Avoid dates in slugs. Dates belong in the note body.

## Note Template

Each note should include these sections:

```markdown
# <Title>

- Date:
- Signature:
- Stage:
- Category:
- Scope:

## Symptoms

## Root cause

## Fix pattern

## Applied change

## Verification

## Files touched

## Related notes
```

The signature should be a short grep-friendly string copied from the primary failure.

Keep the note concise. Do not paste entire build logs; include only the minimal lines needed to identify the pattern.

## Scaffold Command

From the repository root:

```bash
python .agents/skills/build-breakage-loop/scripts/scaffold_error_note.py \
  --stage compile \
  --category platform-api-gap \
  --title "QNX fcntl F_GETFL shared memory descriptors report O_RDONLY" \
  --signature "Unexpected(4) = TakeError::kUnexpectedReadOnlyFd"
```

The script creates the normalized note path under `docs/qnx/history/build-errors/` and fills in the standard headings.

## Search Heuristics

When triaging a new error, search in this order:

1. exact error code or symbol name
2. file name or subsystem name
3. API or macro involved
4. dominant cause-class keywords

Examples:

```bash
rg -n "EBADF|posix_spawnp|launch_qnx" docs/qnx
rg -n "F_GETFL|TakeError::kUnexpectedReadOnlyFd" docs/qnx
rg -n "death test|fchdir|cwd_fd" docs/qnx/history
rg -n "allocator_shim|duplicate symbol" docs/qnx/history/build-errors/link
```

## Migration Guidance

Do not recreate a long chronological rollup file. Update the structured notes and keep `docs/qnx/build-error-index.md` short.

Instead:

1. Keep using it as the lightweight entry point.
2. For each newly resolved blocker, create a structured note in the tree.
3. When touching an old high-value incident again, backfill a structured note for it.
4. Add a short cross-reference in the summary log if helpful.

This incremental migration keeps the catalog useful immediately without paying a one-time conversion cost.
