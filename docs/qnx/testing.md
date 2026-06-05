# QNX Testing Workflow

## Prerequisites

- Chromium tree bootstrapped for QNX
- target built in `out/qnx_release`
- QNX SDP 8 installed
- QEMU image available under your local QNX SDK/image directory

## Recommended flow

### 1. Prepare host networking and NFS

```bash
cd <CHROMIUM_SRC_ROOT>
sudo ./cef/tools/qnx_setup_env.sh
```

This prepares the host-side requirements used by the QEMU runner, including `tap0` and NFS export support.

### 2. Build the target

```bash
cd <CHROMIUM_SRC_ROOT>
./out/qnx_release/ninja_qnx.sh base_unittests
```

ANGLE test group:

```bash
cd <CHROMIUM_SRC_ROOT>
./out/qnx_release/ninja_qnx.sh \
  angle_system_info_test angle_unittests angle_end2end_tests
```

### 3. Run the target on QNX

Broad run:

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_run_test.sh --timeout 7200 --kill-existing "*"
```

Focused run:

```bash
./cef/tools/qnx_run_test.sh --timeout 600 --kill-existing \
  "CommandLineTest.CommandLineConstructor"
```

ANGLE broad run:

```bash
./cef/tools/qnx_run_test.sh --angle --timeout 7200 --kill-existing
```

Arbitrary guest command:

```bash
./cef/tools/qnx_run_test.sh --cmd './base_unittests --gtest_list_tests'
```

## Default broad-run exclusions

`cef/tools/qnx_run_test.sh` automatically excludes the current environment-specific failures:

- `StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree`
- `ImportantFileWriterTest.FailedWriteWithObserver`
- `*AnyCriticalThreadHung*`

## Manual QEMU launch

If needed, QEMU can also be started manually:

```bash
cd <QNX_QEMU_DIR>

qemu-system-x86_64 \
  --enable-kvm \
  -drive file=output/disk-qemu,format=raw,if=ide,id=drv0 \
  -netdev tap,id=net0,ifname=tap0,script=no \
  -device virtio-net-pci,netdev=net0 \
  -kernel output/ifs.bin \
  -nographic \
  -serial mon:stdio \
  --cpu host,host-phys-bits-limit=40 \
  -smp 4 -m 4G
```

Login credentials:

- user: `root`
- password: `root`

## Manual guest setup

If you are not using `cef/tools/qnx_run.sh`, set up the guest manually:

```bash
ifconfig vtnet0 10.0.2.2 netmask 255.255.255.0 up
route add default 10.0.2.1
fs-nfs3 10.0.2.1:/export/chromium-src /mnt/nfs
cd /mnt/nfs/out/qnx_release
export LD_LIBRARY_PATH=/mnt/nfs/out/qnx_release
export CHROME_EXE_PATH=/mnt/nfs/out/qnx_release/base_unittests
export CR_SOURCE_ROOT=/mnt/nfs
```

## Common commands

| Purpose | Command |
|---|---|
| boot + mount only | `./cef/tools/qnx_run_test.sh --mount-only --kill-existing` |
| run one test | `./cef/tools/qnx_run_test.sh --timeout 600 'ProcessTest.Create'` |
| run list-tests | `./cef/tools/qnx_run_test.sh --cmd './base_unittests --gtest_list_tests'` |
| run ANGLE group | `./cef/tools/qnx_run_test.sh --angle --timeout 7200 --kill-existing` |
| inspect QEMU interactively | `tmux attach -t <session>` when running inside tmux |

## Logs and results

Typical runner artifacts are written under `out/qnx_release/`:

- `qnx_run_*.log`
- `qnx_run_*.log.serial`
- `qnx_run_*_boot.log`

Quick inspection:

```bash
ls -1t out/qnx_release/qnx_run_*.log | head

tail -n 40 "$(ls -1t out/qnx_release/qnx_run_*.log | head -n 1)"
```

## Known testing caveats

| Issue | Notes |
|---|---|
| QNX `fork()` restrictions | use spawn-based paths; avoid assuming Linux death-test behavior |
| QEMU environment noise | some timing and signal-handler tests remain environment-specific |
| `tap0` missing | rerun `sudo ./cef/tools/qnx_setup_env.sh` |
| stale QEMU instance | use `--kill-existing` |

## Validation pattern

A practical validation loop is:

1. bootstrap with `cef/tools/cef_create_projects_qnx.sh`
2. build with `./out/qnx_release/ninja_qnx.sh <target>`
3. run focused QEMU tests for the changed area
4. rerun broad `base_unittests`
5. check `fixes-and-decisions.md` and `history/` for regressions or prior art
