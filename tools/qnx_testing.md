# QNX QEMU test runners

These scripts are the repo-managed QNX helpers for the Chromium/CEF port.

## Files

- `cef/tools/qnx_setup_env.sh`
  - host-side one-time setup after reboot
  - enables NFSv3, bind-mounts the Chromium tree at `/export/chromium-src`,
    and configures `tap0`
- `cef/tools/qnx_run.sh`
  - generic QEMU + serial + NFS runner for arbitrary commands
  - keeps the legacy "any shell command" UX (still works exactly as
    before; no behavior change)
  - `--detach` (with optional `--qconn-port PORT`): launches the
    post-`--` QNX command in the guest shell as a background job
    (stdout/stderr/stdin redirected to `<HOST_LOG>_app.log`), exits
    the wrapper cleanly, and implicitly keeps QEMU alive. The
    foreground command no longer owns the serial, so post-run
    `qconn`/gdb attach via `target qnx 10.0.2.2:8000` works.
    Use this when you want `qnx_run.sh` to behave like a daemon
    launcher rather than a synchronous runner.
- `cef/tools/qnx_run_test.sh`
  - dispatcher entry point for the per-module test runner
  - thin shim over `python3 tools/qnx_tests/cli.py "$@"`
- `cef/tools/qnx_tests/`
  - implementation: shared QEMU/serial/login code plus one file per
    test target (base, v8, swiftshader)
  - adding a new target = drop a new file under `qnx_tests/modules/`
    and add it to `MODULES` in `qnx_tests/modules/__init__.py`
- `cef/tools/qnx_run_v8_unittests.py`
  - **backwards-compatibility shim** for the historical v8-only runner
  - forwards all args to `qnx_run_test.sh --v8`; prints a deprecation
    note to stderr
  - the canonical command is `qnx_run_test.sh --v8`

## Host setup

```bash
sudo ./cef/tools/qnx_setup_env.sh
```

## Default build directory

Unless overridden, the runners use:

```bash
BUILD_DIR=out/qnx_release
```

Override when needed:

```bash
BUILD_DIR=/absolute/path/to/out/qnx_x64 ./cef/tools/qnx_run_test.sh --base
```

## System EGL preload

CEF binaries can resolve the generated ANGLE `libEGL.so` even when launched
with `--use-gl=egl`. Use the generic runner's convenience option to force the
QNX system EGL implementation for a bounded runtime check:

```bash
./cef/tools/qnx_run.sh --virgl --kill-existing --preload-system-egl -- \
  ./cefsimple --ozone-platform=qnx --use-gl=egl --no-sandbox --use-native
```

This is equivalent to `--env LD_PRELOAD=/usr/lib/libEGL.so.1`. Do not combine
the two forms; the runner rejects conflicting `LD_PRELOAD` configuration.

## Module overview

| Flag | Binary | Strategy | Notes |
|------|--------|----------|-------|
| `--base`         | `base_unittests`                          | single (1 binary run) | Default. Applies the QNX env exclusions. |
| `--v8`           | `v8_unittests`                            | per-test              | Sidesteps V8's `WithDefaultPlatformMixin` one-way state machine. |
| `--swiftshader`  | `swiftshader_reactor_subzero_unittests`   | per-test              | Stack-sensitive. |
| `--all`          | (all of the above, in one QEMU session)   | mixed                 | Sequential; accumulates pass/fail. |
| `--list`         | (no run)                                  | —                     | Show registered modules. |

When no module flag is given the runner defaults to `--base` (preserves
the historical `qnx_run_test.sh` UX).  A positional gtest filter is
applied to the first selected module.

## Common usage

List modules:

```bash
./cef/tools/qnx_run_test.sh --list
```

Broad run of `base_unittests` (default module):

```bash
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"
```

Single test (still in `base_unittests`):

```bash
./cef/tools/qnx_run_test.sh 'ProcessTest.Create'
```

Filter beginning with `-`:

```bash
./cef/tools/qnx_run_test.sh -- '-*DeathTest*'
```

Run the V8 unittests (per-test, one binary invocation per test):

```bash
./cef/tools/qnx_run_test.sh --v8 --timeout 7200 --kill-existing
```

Run the SwiftShader Subzero reactor unittests:

```bash
./cef/tools/qnx_run_test.sh --swiftshader --timeout 7200 --kill-existing
```

Run every validated module in a single QEMU session:

```bash
./cef/tools/qnx_run_test.sh --all --timeout 7200 --kill-existing
```

V8 focused filter:

```bash
./cef/tools/qnx_run_test.sh --v8 --filter 'InspectorTest.*'
```

Stack-overflow-sensitive test override (V8; see fixes §27):

```bash
./cef/tools/qnx_run_test.sh --v8 --stack-size=384
```

Arbitrary command in the guest build directory (legacy `qnx_run.sh` UX):

```bash
./cef/tools/qnx_run_test.sh --cmd './base_unittests --gtest_list_tests'
```

Boot + mount only, leave QEMU running:

```bash
./cef/tools/qnx_run_test.sh --mount-only --keep-qemu
```

Backward-compatible v8 entry (prints a deprecation note):

```bash
./cef/tools/qnx_run_v8_unittests.py --filter 'InspectorTest.*'
```

## Default exclusions (per module)

`--base` automatically applies the current environment-specific
exclusions:

- `StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree`
- `ImportantFileWriterTest.FailedWriteWithObserver`
- `*AnyCriticalThreadHung*`

Pass `--no-default-exclusions` to disable.  `--v8` does not need a
gtest-filter exclusion list because the per-test strategy parses
`v8/test/unittests/unittests.status` (`[ALWAYS, …]` and
`['system == qnx', …]` sections) and removes unconditionally
`[SKIP]`-annotated tests from the run list before invoking the
binary.

## tmux example

To keep the pane visible after the run finishes:

```bash
tmux new-session -s qnx-base 'bash -lc "cd <CHROMIUM_SRC_ROOT> && ./cef/tools/qnx_run_test.sh --base --timeout 7200 --kill-existing; code=$?; echo __EXIT_CODE__:$code; exec bash"'
```

Attach later:

```bash
tmux attach -t qnx-base
```

## Notes

- The runner mounts `/export/chromium-src` as `/mnt/nfs` in the guest.
- It changes to the guest-side equivalent of `BUILD_DIR` automatically.
- The QEMU session is shared across modules when `--all` is used; only
  one boot/login per invocation.
- Per-test timeout defaults to 600s; per-batch (broad) timeout
  defaults to 7200s.  Both are overrideable via `--timeout`.

## Native EGL launch with `--detach` (cefsimple, gdb-friendly)

`--detach` solves a problem the legacy foreground UX hits on long-lived
guest commands: once `qnx_run.sh` returns, the foreground guest
process may still be holding the serial console, and reattaching
serial or running a fresh `qnx_run.sh` fails. With `--detach`, the
guest command is launched in the guest shell as a background job,
its stdout/stderr/stdin are redirected to a log file in `BUILD_DIR`,
the wrapper exits cleanly, and QEMU stays running. `--qconn-port`
also starts (or reuses) `qconn` on the specified port in the guest,
so a host `gdb` can `target qnx 10.0.2.2:<port>` without competing
with the foreground command for serial.

Example (native EGL `cefsimple --url=about:blank`, runs as a detached
guest job with `qconn` on port 8000 ready for gdb attach):

```bash
./tools/qnx_run.sh --virgl --preload-system-egl --with-input --kill-existing \
    --detach --qconn-port 8000 -- \
    ./cefsimple --ozone-platform=qnx --use-gl=egl --use-native --no-sandbox \
              --enable-logging=stderr --v=1 --vmodule=qnx_platform_event_source=2 \
              --ozone-qnx-gpu-trace --url=about:blank
```

After the wrapper exits, QEMU is still running and the host can:

- Confirm the guest job is alive:
  `ssh qemu@10.0.2.15 'pidin | grep cefsimple'` (or via the qnx serial
  console if it's free).
- Read the app's log:
  `tail -f out/qnx_release/qnx_run_<ts>_cefsimple_app.log`
- Attach `gdb` without serial involvement:
  `gdb -ex 'target qnx 10.0.2.2:8000' -ex 'attach <pid>'`
- Send QMP pointer events:
  `socat - UNIX-CONNECT:/tmp/qnx-qmp.sock` and use
  `input-send-event` (abs `x`/`y`, btn `left`/`middle`/`right`).

If `qconn` was already running on the chosen port from a prior session,
`--detach` reuses it and prints `QCONN_REUSED=1 QCONN_PID=<pid>`.
If a fresh `qconn` is launched, the wrapper prints
`QCONN_LAUNCHED=1 QCONN_PORT=<port> QCONN_PID=<pid> QCONN_LOG=<path>`.
In both cases, the main command's PID is printed as
`APP_PID=<pid> APP_LOG=<path> APP_ALIVE=0|1`.

## Adding a new module

1. Drop a file under `cef/tools/qnx_tests/modules/`, e.g. `cctest.py`,
   containing a `@dataclass` subclass of `qnx_tests.registry.TestModule`.
   Minimum required fields: `name`, `description`, `binary`.  Optional
   overrides: `strategy` (`single` or `per_test`),
   `default_exclusions`, `per_test_args`, `parse_status_file`.
2. Register it in `cef/tools/qnx_tests/modules/__init__.py`:
   ```python
   from .cctest import CCTESTModule
   MODULES = {
       "base": BaseModule(),
       "v8": V8Module(),
       "swiftshader": SwiftShaderModule(),
       "cctest": CCTESTModule(),
   }
   ```
3. The new `--cctest` flag is wired up automatically.  See
   `tools/qnx_tests/modules/v8.py` for a worked `per_test` example
   (status-file parsing, one-process-per-test) and
   `tools/qnx_tests/modules/base.py` for a `single` example
   (default-exclusion logic).
