# Duplicate remap close actions cause QNX posix_spawnp EBADF

- Date: 2026-05-23
- Signature: posix_spawnp EBADF from duplicated remap_sources_to_close entries
- Stage: test
- Category: runtime-assumption
- Scope: base/process launcher

## Symptoms

- Batch-mode launcher spawns failed with `EBADF`.
- Tests in the failed batch showed `no test result` and were skipped.

## Root cause

- The same source fd could be remapped to both stdout and stderr.
- That source fd was added twice to `remap_sources_to_close`.
- QNX rejected the second close action during spawn.

## Fix pattern

- Deduplicate close-action lists before handing them to `posix_spawnp`.
- Treat repeated remap sources as a data-structure hygiene bug, not as a platform quirk to ignore.

## Applied change

- Added `std::sort()` plus `std::unique()` to deduplicate `remap_sources_to_close`.

## Verification

- Hundreds of prior `EBADF` occurrences disappeared.
- Batch mode with `parallel_jobs=4` worked again.

## Files touched

- `base/process/launch_qnx.cc`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/launch-qnx-posix-spawn-too-many-close-actions.md`
