# QNX launch posix_spawnp EBADF from close actions

- Date: 2026-06-12
- Signature: `posix_spawnp(/mnt/nfs/out/qnx_release/mojo_unittests): -9 Bad file descriptor`
- Stage: test
- Category: runtime-assumption
- Scope: `base/process/launch_qnx.cc`, `mojo_unittests`, multiprocess test child launch

## Symptoms

Running `mojo_unittests` under QEMU reported many crashed/skipped tests. The first actionable failure was:

```text
[ RUN      ] EmbedderTest.MultiprocessChannels
[...:ERROR:base/process/launch_qnx.cc:381] posix_spawnp(/mnt/nfs/out/qnx_release/mojo_unittests): -9 Bad file descriptor
[1/1] EmbedderTest.MultiprocessChannels (CRASHED)
```

After that failure, the launcher reported missing results for following tests, so the apparent large crash set was mostly fallout from failed child process creation.

## Root cause

`mojo` multiprocess tests pass a `PlatformChannel` endpoint to the child through `LaunchOptions::fds_to_remap`. The QNX `LaunchProcess` implementation added a close-superfluous-FDs sweep whenever `fds_to_remap` was non-empty, creating many `posix_spawn_file_actions_addclose()` entries.

On QNX, that close action set can make `posix_spawnp()` fail with `EBADF` when combined with descriptor remapping and the NFS-backed test executable. This violated the file's own intended QNX behavior: remap requested descriptors, close original remap sources, but do not emulate Chromium's POSIX close-superfluous-FDs fork path.

## Fix pattern

For QNX `posix_spawnp()` launch:

- keep explicit `dup2` remaps from `fds_to_remap`
- close only the original remap source descriptors after duplication
- do not scan the parent descriptor table and add close actions for every other open FD
- otherwise preserve fork+exec-like descriptor inheritance semantics

## Applied change

Removed the close-superfluous-FDs sweep from `base/process/launch_qnx.cc` and left an explanatory comment so it is not reintroduced.

Durable source-of-truth file:

- `cef/patch/qnx/chromium/new_files/base/process/launch_qnx.cc`

## Verification

Rebuilt `mojo_unittests`:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source ./qnx_env.sh
ninja mojo_unittests
```

Verified the isolated reproducer:

```bash
cd /home/yuta/chromium/test/src/cef
./tools/qnx_run_test.sh --mojo --kill-existing --filter 'EmbedderTest.MultiprocessChannels' --timeout 240
```

Result:

```text
[ RUN      ] EmbedderTest.MultiprocessChannels
[       OK ] EmbedderTest.MultiprocessChannels (53 ms)
__PI_QNX_EXIT__:0
```

Verified the full `mojo_unittests` run through the QNX test runner:

```bash
cd /home/yuta/chromium/test/src/cef
./tools/qnx_run_test.sh --mojo --kill-existing --timeout 900
```

Result included all tests through:

```text
[1467/1467] All/InvitationCppTest.ProcessErrors/2 (66 ms)
__PI_QNX_EXIT__:0
```

## Files touched

- `cef/patch/qnx/chromium/new_files/base/process/launch_qnx.cc`
- `cef/docs/qnx/history/build-errors/test/runtime-assumption/qnx-launch-posix-spawnp-ebadf-from-close-actions.md`

## Related notes

- `docs/qnx/history/status/todo-2026-05-16.md` mentions earlier QNX batch-mode launcher issues.
