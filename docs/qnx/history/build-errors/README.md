# Build Error Catalog

This directory stores retrieval-oriented notes for resolved or well-understood breakages.

## Layout

Notes live under:

```text
<stage>/<category>/<slug>.md
```

Examples:

- `compile/platform-api-gap/qnx-fcntl-getfl-shm-fd.md`
- `compile/feature-guard/fieldtrial-to-struct-platform-qnx-choice.md`
- `compile/build-graph/cpuinfo-qnx-fork-path-switch.md`
- `gn/build-graph/ffmpeg-qnx-platform-config-directory-missing.md`
- `link/build-graph/libdrm-memstream-link-dependency-and-makedev-wrapper.md`
- `test/runtime-assumption/googletest-death-test-cwd-fd-invalidation.md`
- `test/runtime-assumption/launch-qnx-posix-spawn-too-many-close-actions.md`

## How to use

- Start from the current failure's stage: `bootstrap`, `gn`, `compile`, `link`, `package`, or `test`.
- Narrow by the dominant cause class.
- Use `rg` with the primary error code, API name, file, or symbol.

Examples:

```bash
rg -n "TakeError::kUnexpectedReadOnlyFd|F_GETFL" docs/qnx/history/build-errors
rg -n "death test|fchdir|cwd_fd|ENOTDIR" docs/qnx/history/build-errors/test
rg -n "posix_spawnp|EBADF|close_superfluous_fds" docs/qnx/history/build-errors
rg -n "open_memstream|libmemstream|makedev" docs/qnx/history/build-errors/link
```

## Relationship to other docs

- `docs/qnx/build-error-index.md` is the lightweight search entry point.
- `docs/qnx/history/build-errors/` is optimized for search and reuse by agents.
