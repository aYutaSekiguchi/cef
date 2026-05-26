# QNX Chromium Port: Spawn-based LaunchProcess — Handoff Document

Created: 2026-05-17
Audience: Subsequent maintainers

## Project Overview

On QNX Neutrino, `fork()` after `pthread_create()` returns `ENOSYS` (C library does not support fork in multithreaded state), so Chromium's `base::LaunchProcess()` was replaced with a `posix_spawn()` / QNX `spawn()`-based implementation.

## Prerequisite: QNX Fork Restriction Details

QNX SDP 8 official documentation:
- https://www.qnx.com/developers/docs/8.0/com.qnx.doc.neutrino.getting_started/topic/s1_procs_Creation_and_threads.html
- `fork()` after `pthread_create()` returns `ENOSYS`
- Behavior permitted by POSIX
- Recommendation: use `posix_spawn()` / `spawn()` for multithreaded processes
- `spawn()` child does not inherit parent threads (child is single-threaded)
- Child does not inherit QNX message-passing objects / channels / coids

The QNX restriction applies to real hardware, not just QEMU environments.

## Completed Deliverables

### 1. `base/process/launch_qnx.cc` (New file, ~300 lines)

`posix_spawnp()`-based `LaunchProcess` implementation.

| Feature | Status | Implementation |
|---------|--------|----------------|
| `LaunchProcess(argv, LaunchOptions)` | ✅ | `posix_spawnp()` |
| `GetAppOutput*` | ✅ | Temp file based (no pipe) |
| `fds_to_remap` | ✅ | `posix_spawn_file_actions_adddup2()` |
| `environment` / `clear_environment` | ✅ | `AlterEnvironment()` |
| stdin `/dev/null` | ✅ | `posix_spawn_file_actions_addopen()` |
| `new_process_group` | ✅ | `POSIX_SPAWN_SETPGROUP` |
| `current_directory` | ✅ | Parent-side `chdir()` + `fchdir()` restore |
| `LD_LIBRARY_PATH` fix | ✅ | Prepend absolute path before chdir |
| `wait` | ✅ | `waitpid()` after spawn |
| Relative path resolution | ✅ | `./base_unittests` → `/mnt/nfs/base_unittests` |
| `POSIX_SPAWN_SETSIGDEF` | ✅ | Reset signal handlers to SIG_DFL in child |
| `pre_exec_delegate` | ❌ unsupported | No fork-child callback in spawn |
| `maximize_rlimits` | ❌ unsupported | Explicitly errors + logs |

### 2. `base/BUILD.gn` Changes

- `is_posix && !is_apple` block: QNX also includes `launch_posix.cc` (QNX excluded via `sources -=`).
- `is_qnx` block: `sources -= ["process/launch_posix.cc"]` + `sources += ["process/launch_qnx.cc"]`.
- Profiler / madv_free exclusions continued.

### 3. GTest Death Test Fixes (`third_party/googletest/src/`)

| Fix | File | Content |
|-----|------|---------|
| `O_DIRECTORY` for cwd_fd | `gtest-death-test.cc` | QNX 8.0+ requires `O_DIRECTORY` for `fchdir` |
| `spawn` failure handling | `gtest-death-test.cc` | `child_pid == -1` sets `spawned(false)` |
| Zombie reaping | `gtest-death-test.cc` | `while(waitpid(-1,...)>0)` + `usleep(1000)` |
| FILE* use-after-free | `gtest-port-wrapper.cc` | Replaced `fopen/fclose/ReadEntireFile` with `open/read/close` |

Note: Removing `GTEST_OS_QNX` from `gtest-port.h` **failed** (compilation errors). Death test disabling handled via runtime filter instead.

### 4. Documentation updates

Recorded spawn-based LaunchProcess design decisions, fix history, and follow-up items.

## Test Results

### `base_unittests` Status

Filter: `--gtest_filter=-*DeathTest* --single-process-tests`

| Test Group | Count | Status |
|------------|-------|--------|
| LaunchTest | 1 | ✅ PASS |
| ProcessUtilTest (CurrentDirectory, GetAppOutput, LaunchProcess) | 4 | ✅ PASS |
| TestLauncherTest | 31 | ✅ PASS |
| CheckDerefTest (RawPointerDeath, etc.) | 6 | ✅ PASS (spawn working) |
| BarrierCallbackTest (ErrorToCallCallbackWithZeroCallbacks) | 9 | ✅ PASS |
| BarrierClosureTest (ChecksIfCalledForZeroClosures) | 5 | ✅ PASS |
| AutoSpanificationIncrementTest (PreIncrementEmptySpan, etc.) | 8 | ❌ CRASH (investigation needed) |
| Other death tests (EXPECT_DEATH not named `*DeathTest*`) | Many | ⚠️ PASS/CRASH mixed |

### `*DeathTest*`-Named Tests

Exclusion recommended with `--gtest_filter=-*DeathTest*`. These tests use GTest **fast-style** (`NoExecDeathTest`) which directly calls `fork()`. Always fails on QNX in multithreaded state due to `ENOSYS`.

## Unresolved Issues

### P0: Heap Corruption

**Symptoms**: After running many tests, intermittent SIGSEGV at different locations (`0xcdcdcdcdcdcdcdcd` pattern).
**Stack examples**:
- `CapturedStream::GetCapturedString()` → `fdopen()` → `pthread_mutex_lock(0xcdcdcdcdcdcdcdcd)`
- `ThreadTypeManager::MaybeUpdate()` → `raise_leases_.GetHighestLease()` crash.
**Root cause identified**: PartitionAlloc's freed-memory fill (0xcdcdcdcd) displayed. Some test is corrupting the heap (buffer overflow or use-after-free) accumulating over time.
**Workaround**: Fix to avoid fopen/FILE structure allocation was applied. Identifying the root-cause test requires binary search + gdb. An AddressSanitizer build would be ideal.
**Standalone test**: `ScopedThreadPriorityDeathTest.NoRealTime` SIGSEGVs even in isolation → either a bug in the test itself or heap corruption during global initialization. May not be QNX-specific.

### P1: Post-Death Test Crashes

**Symptom**: After a few successful death tests, subsequent death tests get SIGSEGV.
**Root cause**: GTest `CapturedStream` is freed by `GetCapturedStderr()` and re-referenced by `Passed()` — a use-after-free. The FILE structure's mutex gets overwritten by PartitionAlloc's fill pattern (0xcdcdcdcd).
**Workaround**: Replaced `fopen` with `open/read` in `gtest_port_wrapper.cc`. Heap corruption root cause resolution will likely improve this.

### P2: Exclusion Filter Maintenance

Current recommended filter:
```
--gtest_filter='-*DeathTest*:*ScopedThreadPriority*:*NoRealTime*'
--single-process-tests
```

Tests with death assertions other than `*DeathTest*` names (`ASSERT_DEATH_IF_SUPPORTED`, etc.) use GTest threadsafe-style (spawn) and may not need exclusion, but can crash due to resource exhaustion. Once heap corruption is resolved, all tests should theoretically PASS, so filter expansion is a temporary workaround.

## Commit History

```
bf4b347  QNX death test: add zombie reaping and usleep before spawn
d8f2d49  QNX death test: fix __QNX__ version detection (O_DIRECTORY)
7cd15b7  QNX death test: QNX O_DIRECTORY fix for death test
d27e509  QNX death test: spawn failure handling
c8c9c0e  QNX spawn: use temp file instead of pipe for GetAppOutput capture
bfc8142  QNX spawn: set FD_CLOEXEC on GetAppOutput pipe
a38e516  QNX spawn: initialize pid to 0 before posix_spawnp
12628b4  QNX spawn: add POSIX_SPAWN_SETSIGDEF
bcdb066  QNX: Fix relative path resolution and LD_LIBRARY_PATH
a137679  QNX: Implement spawn-based base::LaunchProcess
```

`base/process/launch_qnx.cc` is the main new file. GTest fixes in `third_party/googletest/src/` (submodule) + `third_party/googletest/custom/` (Chromium wrapper).

## References

- `cef/docs/qnx/build-and-toolchain.md` — current toolchain and design decisions
- `cef/docs/qnx/fixes-and-decisions.md` — current fix log
- `cef/docs/qnx/status.md` — current validation status and follow-up checklist
- `cef/docs/qnx/history/research/` — investigative materials
- `base/process/launch_qnx.cc` — Implementation code
- `base/process/launch_posix.cc` — Replaced fork+exec implementation
- `base/process/launch_mac.cc` — Reference posix_spawn implementation (macOS)