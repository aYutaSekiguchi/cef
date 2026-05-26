# QNX QEMU test runners

These scripts are the repo-managed QNX helpers for the Chromium/CEF port.

## Files

- `cef/tools/qnx_setup_env.sh`
  - host-side one-time setup after reboot
  - enables NFSv3, bind-mounts the Chromium tree at `/export/chromium-src`, and configures `tap0`
- `cef/tools/qnx_run.sh`
  - generic QEMU + serial + NFS runner for arbitrary commands
- `cef/tools/qnx_run_test.sh`
  - convenience wrapper for gtest-style binaries such as `base_unittests`

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
BUILD_DIR=/absolute/path/to/out/qnx_x64 ./cef/tools/qnx_run_test.sh
```

## Common usage

Broad run of `base_unittests`:

```bash
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing
```

Single test:

```bash
./cef/tools/qnx_run_test.sh ProcessTest.Create
```

Filter beginning with `-`:

```bash
./cef/tools/qnx_run_test.sh -- '-*DeathTest*'
```

Arbitrary command in the guest build directory:

```bash
./cef/tools/qnx_run.sh -- ./base_unittests --gtest_list_tests
```

Boot + mount only, leave QEMU running:

```bash
./cef/tools/qnx_run.sh --mount-only --keep-qemu
```

## tmux example

To keep the pane visible after the run finishes:

```bash
tmux new-session -s qnx-base 'bash -lc "cd <CHROMIUM_SRC_ROOT> && ./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing; code=$?; echo __EXIT_CODE__:$code; exec bash"'
```

Attach later:

```bash
tmux attach -t qnx-base
```

## Notes

- The runner mounts `/export/chromium-src` as `/mnt/nfs` in the guest.
- It changes to the guest-side equivalent of `BUILD_DIR` automatically.
- `qnx_run_test.sh` appends the known environment-only exclusions by default:
  - `StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree`
  - `ImportantFileWriterTest.FailedWriteWithObserver`
  - `*AnyCriticalThreadHung*`
