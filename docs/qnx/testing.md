# QNX Testing Workflow

## Prerequisites

- Chromium tree bootstrapped for QNX
- target built in `out/qnx_release`
- QNX SDP 8 installed
- QEMU image available under your local QNX SDK/image directory

## Recommended flow

`cef/tools/qnx_run_test.sh` remains the legacy TAP+NFS module dispatcher. Recognized module flags
are `--base`, `--ceftests`, `--v8`, `--swiftshader`, `--angle`, `--all`, and
`--list`.  If no module flag is provided, the runner defaults to `--base` for
backward compatibility.

### 1. Rootless QEMU setup (default)

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_run.sh -- ./base_unittests --gtest_filter=ProcessTest.Create
```

`qnx_run.sh` now uses QEMU's native `passt` backend and an unprivileged local
HTTP server. It packages the requested binary (or the `cefsimple` runtime
manifest), the guest downloads it through passt, and extracts it under
`/data/qnx_payload`. No TAP device, NFS export, forwarding rule, or `sudo` is
required. Install the host `passt` package and ensure the user can access KVM.

The old TAP+NFS path remains available explicitly:

```bash
sudo ./cef/tools/qnx_setup_env.sh
./cef/tools/qnx_run.sh --net-backend tap --payload-mode nfs -- ./base_unittests
```

### Guest DNS and external network

The rootless runner uses passt networking. `qnx_run.sh` automatically
select the first non-loopback DNS server reported by `resolvectl`. Override
that choice when required by the host network or VPN:

```bash
QNX_DNS_SERVER=192.168.0.1 ./cef/tools/qnx_run.sh --virgl -- \
  ./cefsimple --use-native --ozone-platform=qnx --no-sandbox
```

The equivalent explicit option is `--dns-server 192.168.0.1`. Loopback,
multicast, link-local, and unspecified addresses are rejected because the
QNX guest cannot use the host's `127.0.0.53` systemd-resolved stub directly.
The guest resolver file is populated after the static address and default
route are configured.

Before diagnosing Chromium, verify the independent network layers:

```bash
./cef/tools/qnx_run.sh --timeout 30 -- \
  'ifconfig vtnet0; netstat -rn; cat /etc/resolv.conf; \
   ping -c1 -W2 192.168.0.1; ping -c1 -W2 8.8.8.8; \
   timeout 15 getent hosts google.com'
```

`192.168.0.1` is passt's default guest-visible host/gateway address. The QNX image does
not contain a guest `timeout` utility; bound application runs with the
runner's host-side `--timeout` option instead of prefixing the guest command
with `timeout`.

### 2. Build the target

```bash
cd <CHROMIUM_SRC_ROOT>
./out/qnx_release/ninja_qnx.sh base_unittests
```

CEF API tests:

```bash
cd <CHROMIUM_SRC_ROOT>
./out/qnx_release/ninja_qnx.sh ceftests
```

ANGLE test group:

```bash
cd <CHROMIUM_SRC_ROOT>
./out/qnx_release/ninja_qnx.sh \
  angle_system_info_test angle_unittests angle_end2end_tests
```

### 3. Run the target on QNX

Base broad run:

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_run.sh --timeout 7200 -- ./base_unittests
```

Base focused run:

```bash
./cef/tools/qnx_run_test.sh --base --timeout 600 --kill-existing \
  "CommandLineTest.CommandLineConstructor"
```

CEF API focused run:

```bash
./cef/tools/qnx_run_test.sh --ceftests --timeout 600 --kill-existing \
  "DownloadTest.*"
```

ANGLE broad run:

```bash
./cef/tools/qnx_run_test.sh --angle --timeout 7200 --kill-existing
```

List registered modules:

```bash
./cef/tools/qnx_run_test.sh --list
```

Arbitrary guest command:

```bash
./cef/tools/qnx_run_test.sh --cmd './base_unittests --gtest_list_tests'
```

## Default base broad-run exclusions

The default `--base` module automatically excludes the current
environment-specific failures:

- `StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree`
- `ImportantFileWriterTest.FailedWriteWithObserver`
- `*AnyCriticalThreadHung*`

## GUI / Screen EGL smoke test

`cef/tools/qnx_run.sh` defaults to headless QEMU (`-nographic`) for normal unit
and API test runs.  For Ozone, Screen, EGL, or GLES validation, opt in to a GUI
QEMU launch with virtio/virgl enabled. For agent-driven screenshots and input,
use `--gui`; the complete QMP workflow is documented in
[`gui-testing.md`](gui-testing.md):

```bash
./cef/tools/qnx_run.sh --gui --keep-qemu --mount-only
./cef/tools/qnx_gui.py --json screenshot \\
  --output out/qnx_release/gui-artifacts/boot.png
```

The lower-level graphics-only form remains available:

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_run.sh --virgl --kill-existing -- egl-configs
```

Equivalent long form:

```bash
./cef/tools/qnx_run.sh --qemu-graphics virgl -- egl-configs
```

Expected `egl-configs` output includes a valid Mesa EGL display, for example:

```text
EGL_VENDOR = Mesa Project
EGL_VERSION = 1.5
EGL_CLIENT_APIS = OpenGL_ES
```

For an on-screen GLES smoke test, run `gles2-gears` briefly and capture a guest
screenshot into the NFS-mounted build directory:

```bash
./cef/tools/qnx_run.sh --virgl --kill-existing --timeout 120 -- \
  'gles2-gears >/tmp/gles2-gears.log 2>&1 & \
   sleep 5; \
   screenshot -file=/mnt/nfs/out/qnx_release/qnx-gles2-gears.bmp -verbose; \
   slay gles2-gears 2>/dev/null || true; \
   head -40 /tmp/gles2-gears.log 2>/dev/null || true'
```

The screenshot should appear on the host at:

```text
out/qnx_release/qnx-gles2-gears.bmp
```

Notes:

- `--gui` is shorthand for `--qemu-graphics virgl --with-input`; it also
  prints the QMP socket used by `cef/tools/qnx_gui.py`.
- `--virgl` uses `-vga none -device virtio-vga-gl -display <backend>,gl=on`.
- The default display backend is `gtk`; use `--qemu-display sdl` if SDL works
  better on the host.
- A host GUI session and QEMU OpenGL-capable display backend are required.
- A plain GUI window without `virtio-vga-gl` is not enough for the current QEMU
  image's Screen EGL path; `egl-configs` may fail with an invalid EGL display.

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
printf 'nameserver %s\n' 192.168.0.1 > /etc/resolv.conf
fs-nfs3 10.0.2.1:/export/chromium-src /mnt/nfs
cd /mnt/nfs/out/qnx_release
export LD_LIBRARY_PATH=/mnt/nfs/out/qnx_release
unset CHROME_EXE_PATH  # normally resolved via QNX /proc/self/exefile
export CR_SOURCE_ROOT=/mnt/nfs
```

## Common commands

| Purpose | Command |
|---|---|
| list registered modules | `./cef/tools/qnx_run_test.sh --list` |
| boot + mount only | `./cef/tools/qnx_run_test.sh --mount-only --kill-existing` |
| run one base test | `./cef/tools/qnx_run_test.sh --base --timeout 600 'ProcessTest.Create'` |
| run one CEF API group | `./cef/tools/qnx_run_test.sh --ceftests --timeout 600 'DownloadTest.*'` |
| run list-tests | `./cef/tools/qnx_run_test.sh --cmd './base_unittests --gtest_list_tests'` |
| override guest DNS | `./cef/tools/qnx_run.sh --dns-server 192.168.0.1 -- true` |
| run V8 per-test module | `./cef/tools/qnx_run_test.sh --v8 --timeout 7200 --kill-existing` |
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


## Crash and GDB workflow

Use the QNX guest `gdb` for the first-pass stack trace.  It sees the same
runtime paths as the crashed process (`/mnt/nfs/out/qnx_release`, `/usr/lib`,
etc.), which avoids host/target sysroot drift while triaging crashes.

### Stable interactive session

Boot QEMU, mount the Chromium tree, and keep the guest alive:

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_run_test.sh --mount-only --kill-existing
```

The runner prints the serial attach command, for example:

```bash
socat -,raw,echo=0 TCP:127.0.0.1:10024
```

Run interactive `gdb` from this serial shell, not through `--cmd`.  The
`--cmd` path is best for non-interactive/batch commands because it wraps the
command and waits for an exit marker.

### Capture a core for a crash

From the QNX serial shell:

```sh
cd /mnt/nfs/out/qnx_release
rm -f ceftests*.core base_unittests*.core *.core
ulimit -c unlimited

# Keep dumper running before reproducing the crash.  -I includes the pid in
# the file name, -n avoids overwriting, and -v prints the selected path.
dumper -d /mnt/nfs/out/qnx_release -I -n -v &
echo $! >/tmp/qnx-dumper.pid

./ceftests \
  --ozone-platform=headless \
  --disable-gpu \
  --disable-gpu-compositing \
  --gtest_filter=BrowserSettingsTest.JavaScriptDisabled

echo exit=$?
sleep 1
ls -lt *.core | head
kill $(cat /tmp/qnx-dumper.pid) 2>/dev/null || true
```

For a hang or timeout with no crash, dump the live process instead:

```sh
pidin ar | grep ceftests
# Replace <pid> with the process to inspect, such as the browser or renderer.
dumper -p <pid> -d /mnt/nfs/out/qnx_release -I -n -v
ls -lt *.core | head
```

### Get a stack trace in guest gdb

Use the crashed executable that matches the core.  CEF child processes normally
use the same executable with `--type=renderer`, `--type=gpu-process`, etc., so
`ceftests` is usually still the right executable for `ceftests-<pid>.*.core`.

Interactive form:

```sh
cd /mnt/nfs/out/qnx_release
gdb -q ./ceftests ceftests-<pid>.<seq>.core
```

Useful commands inside `gdb`:

```gdb
set pagination off
set print thread-events off
bt 60
frame 0
info args
info locals
quit
```

Do not run `info threads` followed by `thread apply all bt`.  `libcef.so`
ships huge `.debug_ranges` (about 120 MB) and `.debug_frame` (about 50 MB)
sections that the QNX guest `gdb` cannot fit in its address space; iterating
over every thread also walks all of them, which exhausts guest `gdb`
virtual memory and produces `virtual memory exhausted: can't allocate NNNN bytes`.
Stick to a single-thread `bt` and use the host-side cross-`gdb` fallback
when full multi-thread state is needed.

Batch form, which is safer for logs and for `qnx_run_test.sh --cmd`:

```sh
cd /mnt/nfs/out/qnx_release
core=$(ls -1t ceftests*.core *.core 2>/dev/null | head -1)
gdb -q -batch -nx \
  -ex 'set sysroot /home/yuta/qnx800/target/qnx/x86_64' \
  -ex 'set solib-search-path /home/yuta/chromium/src/out/qnx_release:/home/yuta/qnx800/target/qnx/x86_64/usr/lib:/home/yuta/qnx800/target/qnx/x86_64/lib' \
  -ex 'file ./ceftests' \
  -ex "core-file $core" \
  -ex 'set pagination off' \
  -ex 'set print thread-events off' \
  -ex 'bt 60' \
  -ex 'frame 0' \
  ./ceftests \
  | tee /mnt/nfs/out/qnx_release/gdb-ceftests.txt
```

Notes:

- `-nx` skips `.gdbinit` so guest `gdb` does not eagerly load `libcef.so`'s
  full `.debug_ranges` section.  Without `-nx` the session runs out of
  virtual memory before producing a backtrace.
- `set sysroot` and `set solib-search-path` redirect shared-library lookup
  to the SDP target tree and the local `out/qnx_release`, so symbol
  resolution does not require loading `libcef.so`'s debug info at all.
- `file ./ceftests` followed by `core-file $core` loads the executable and
  core explicitly after the sysroot is in place.

### One-shot command from the host

For a quick non-interactive check without attaching serial:

```bash
cd <CHROMIUM_SRC_ROOT>
./cef/tools/qnx_run_test.sh --cmd '
cd /mnt/nfs/out/qnx_release &&
core=$(ls -1t ceftests*.core *.core 2>/dev/null | head -1) &&
gdb -q -batch -nx \
  -ex "set sysroot /home/yuta/qnx800/target/qnx/x86_64" \
  -ex "set solib-search-path /home/yuta/chromium/src/out/qnx_release:/home/yuta/qnx800/target/qnx/x86_64/usr/lib:/home/yuta/qnx800/target/qnx/x86_64/lib" \
  -ex "file ./ceftests" \
  -ex "core-file $core" \
  -ex "set pagination off" \
  -ex "set print thread-events off" \
  -ex "bt 60" \
  -ex "frame 0" \
  ./ceftests
' --timeout 300 --kill-existing
```

Do not run interactive `gdb` through `--cmd`; it will wait for input and can
hit the runner timeout.  If a command may page output, disable pagination or
pipe it through `head`/`cat` with care.

The QNX guest `gdb` cannot read `libcef.so`'s full `.debug_ranges` section
(it is too large for the guest's address space) and prints a BFD error
similar to:

```text
BFD: error: libcef.so(.debug_ranges) is too large (0x723ae10 bytes)
warning: Can't read data for section '.debug_ranges' in file 'libcef.so'
utils.c:681: internal-error: virtual memory exhausted: can't allocate 4064 bytes
A problem internal to GDB has been detected,
further debugging may prove unreliable
```

When you see this, the backtrace output is empty and only the BFD/GDB error
is printed.  The recommended fix is the sysroot-based invocation shown
above (`-nx`, `set sysroot`, `set solib-search-path`, `file` then
`core-file`) which avoids loading `libcef.so`'s debug info and resolves
symbols from the SDP target tree and the local `out/qnx_release` build
artifacts instead.

The same issue can also be triggered by `thread apply all bt` even when
single-thread `bt` works.

### Host-side fallback

If the guest is unavailable, the QNX host cross-gdb is available in the SDP:

```bash
/home/yuta/qnx800/host/linux/x86_64/usr/bin/x86_64-nto-qnx8.0.0-gdb-14.2 \
  -q out/qnx_release/ceftests out/qnx_release/ceftests-<pid>.<seq>.core
```

Prefer guest `gdb` for initial triage.  If host gdb cannot find QNX shared
libraries, set the target sysroot to the SDP target tree, for example:

```gdb
set sysroot /home/yuta/qnx800/target/qnx/x86_64
set solib-search-path /home/yuta/chromium/src/out/qnx_release:/home/yuta/qnx800/target/qnx/x86_64/usr/lib:/home/yuta/qnx800/target/qnx/x86_64/lib
```

### Cleanup

Core files and GDB transcripts are local investigation artifacts.  Remove them
before reporting or committing:

```bash
rm -f out/qnx_release/*.core out/qnx_release/gdb-*.txt
```

## Known testing caveats

| Issue | Notes |
|---|---|
| QNX `fork()` restrictions | use spawn-based paths; avoid assuming Linux death-test behavior |
| QEMU environment noise | some timing and signal-handler tests remain environment-specific |
| `base_unittests` exclusions | the three default exclusions in `status.md` are for the `--base` broad run, not a blanket policy for every module |
| `ceftests` broad status | focused CEF API groups pass after the recent fontconfig/CORS/V8 fixes, but the whole suite is still a bring-up track |
| `FrameHandlerTest` cross-origin ordering | cross-origin OOP renderer ordering remains an open QNX-specific follow-up; see the structured note before changing expectations |
| ANGLE end-to-end | `angle_end2end_tests` needs a Vulkan-capable guest GPU/surface environment; unit tests are the useful current signal |
| `tap0` missing | rerun `sudo ./cef/tools/qnx_setup_env.sh` |
| stale QEMU instance | use `--kill-existing` |

## Validation pattern

A practical validation loop is:

1. bootstrap with `cef/tools/qnx_sync_sources.sh -f -R` followed by `cef/tools/cef_create_projects_qnx.sh`
2. build with `./out/qnx_release/ninja_qnx.sh <target>`
3. run focused QEMU tests for the changed area (`--base`, `--ceftests`, `--v8`, `--angle`, etc.)
4. rerun the stable broad `--base` baseline when the change could affect core behavior
5. check `build-error-index.md` and `history/build-errors/` for regressions or prior art
