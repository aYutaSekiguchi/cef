#!/usr/bin/env bash
# Run an arbitrary command on QNX inside full-system QEMU.
# Boots QEMU, logs in on the serial console, mounts the Chromium tree via NFS,
# changes into the requested build directory, exports runtime env vars, then
# streams the command output back to the host in real time.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHROMIUM_SRC="${CHROMIUM_SRC:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$CHROMIUM_SRC/out/qnx_release}"
QNX_DIR="${QNX_DIR:-$HOME/qnx800}"
QEMU_DIR="${QEMU_DIR:-$QNX_DIR/images/qemu/qemu}"
SERIAL_PORT="${SERIAL_PORT:-10024}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-120}"
CMD_TIMEOUT="${CMD_TIMEOUT:-1800}"
KEEP_QEMU=0
MOUNT_ONLY=0
KILL_EXISTING=0

usage() {
  cat <<EOF
Usage: $0 [options] [--] command...

Examples:
  $0 ./base_unittests --gtest_filter=ProcessTest.Create
  $0 -- ./base_unittests --gtest_filter=-*DeathTest*
  $0 --keep-qemu -- bash
  $0 --mount-only

Behavior:
  - mounts /export/chromium-src at /mnt/nfs in the guest
  - changes directory to the BUILD_DIR-relative path under /mnt/nfs
  - exports LD_LIBRARY_PATH, CHROME_EXE_PATH, CR_SOURCE_ROOT

Options:
  --timeout SEC        Command timeout after shell is ready (default: $CMD_TIMEOUT)
  --boot-timeout SEC   Boot/login timeout (default: $BOOT_TIMEOUT)
  --serial-port PORT   TCP serial port (default: $SERIAL_PORT)
  --keep-qemu          Leave QEMU running after the command finishes
  --mount-only         Boot QNX, mount NFS, and leave QEMU running
  --kill-existing      Kill any stale qemu-system-x86_64 first
  --env NAME=VALUE     Extra guest environment variable (may repeat)
  -h, --help           Show help

Prerequisite:
  sudo $SCRIPT_DIR/qnx_setup_env.sh
EOF
}

check_file() {
  [[ -e "$1" ]] || { echo "ERROR: missing $1" >&2; exit 1; }
}

# Parse options until -- or first positional.
POSITIONAL=()
EXTRA_ENV=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --timeout)
      CMD_TIMEOUT="${2:?--timeout requires seconds}"
      shift 2
      ;;
    --boot-timeout)
      BOOT_TIMEOUT="${2:?--boot-timeout requires seconds}"
      shift 2
      ;;
    --serial-port)
      SERIAL_PORT="${2:?--serial-port requires a value}"
      shift 2
      ;;
    --keep-qemu)
      KEEP_QEMU=1
      shift
      ;;
    --mount-only)
      KEEP_QEMU=1
      MOUNT_ONLY=1
      shift
      ;;
    --kill-existing)
      KILL_EXISTING=1
      shift
      ;;
    --env)
      EXTRA_ENV+=("${2:?--env requires NAME=VALUE}")
      shift 2
      ;;
    --)
      shift
      break
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -* )
      if [[ ${#POSITIONAL[@]} -gt 0 ]]; then
        POSITIONAL+=("$1")
        shift
      else
        echo "ERROR: unknown option: $1" >&2
        usage >&2
        exit 2
      fi
      ;;
    *)
      POSITIONAL+=("$1")
      shift
      break
      ;;
  esac
done
while [[ $# -gt 0 ]]; do
  POSITIONAL+=("$1")
  shift
done

if [[ "$MOUNT_ONLY" == 1 ]]; then
  QNX_CMD="true"
elif [[ ${#POSITIONAL[@]} -gt 0 ]]; then
  QNX_CMD="${POSITIONAL[*]}"
else
  QNX_CMD=""
fi

for cmd in qemu-system-x86_64 python3; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "ERROR: $cmd not found" >&2; exit 1; }
done

check_file "$QEMU_DIR/output/ifs.bin"
if [[ -n "${QNX_DISK:-}" ]]; then
  check_file "$QNX_DISK"
elif [[ -e "$QEMU_DIR/output/disk-qemu" ]]; then
  QNX_DISK="$QEMU_DIR/output/disk-qemu"
else
  QNX_DISK="$QEMU_DIR/output/disk-qemu.vmdk"
  check_file "$QNX_DISK"
fi
check_file /export/chromium-src
check_file "$BUILD_DIR"
ip link show tap0 >/dev/null 2>&1 || {
  echo "ERROR: tap0 not found. Run: sudo $SCRIPT_DIR/qnx_setup_env.sh" >&2
  exit 1
}

case "$BUILD_DIR" in
  "$CHROMIUM_SRC") BUILD_DIR_REL="." ;;
  "$CHROMIUM_SRC"/*) BUILD_DIR_REL="${BUILD_DIR#$CHROMIUM_SRC/}" ;;
  *)
    echo "ERROR: BUILD_DIR must live under CHROMIUM_SRC" >&2
    echo "  CHROMIUM_SRC=$CHROMIUM_SRC" >&2
    echo "  BUILD_DIR=$BUILD_DIR" >&2
    exit 1
    ;;
esac
GUEST_BUILD_DIR="/mnt/nfs/$BUILD_DIR_REL"

if [[ "$KILL_EXISTING" == 1 ]]; then
  pkill -9 -f qemu-system-x86_64 2>/dev/null || true
  sleep 1
elif pgrep -f qemu-system-x86_64 >/dev/null 2>&1; then
  echo "ERROR: qemu-system-x86_64 is already running." >&2
  echo "       Stop it first, or pass --kill-existing if it is stale." >&2
  exit 1
fi

if (echo >/dev/tcp/127.0.0.1/$SERIAL_PORT) >/dev/null 2>&1; then
  echo "ERROR: serial port $SERIAL_PORT is already in use." >&2
  exit 1
fi

stamp="$(date +%Y%m%d_%H%M%S)"
safe_label="qnxrun"
if [[ ${#POSITIONAL[@]} -gt 0 ]]; then
  safe_label="${POSITIONAL[0]##*/}"
fi
safe_label="${safe_label//[^A-Za-z0-9_.-]/_}"
safe_label="${safe_label:0:40}"
RESULT_NAME="qnx_run_${stamp}_${safe_label}.log"
HOST_LOG="$BUILD_DIR/$RESULT_NAME"
BOOT_LOG="$BUILD_DIR/${RESULT_NAME%.log}_boot.log"
SERIAL_LOG="$HOST_LOG.serial"
rm -f "$HOST_LOG" "$BOOT_LOG" "$SERIAL_LOG"

# Prefer the basename of the first command token for CHROME_EXE_PATH, but
# allow callers to override via --env CHROME_EXE_PATH=...
GUEST_MAIN_BINARY="base_unittests"
if [[ -n "$QNX_CMD" ]]; then
  first_token="${QNX_CMD%% *}"
  first_token="${first_token%%;*}"
  first_token="${first_token##*/}"
  if [[ -n "$first_token" ]]; then
    GUEST_MAIN_BINARY="$first_token"
  fi
fi

cleanup() {
  status=$?
  if [[ -n "${QEMU_PID:-}" && "$KEEP_QEMU" != 1 ]]; then
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
  fi
  exit "$status"
}
trap cleanup EXIT INT TERM

echo "=== QNX QEMU runner ==="
echo "QEMU dir:  $QEMU_DIR"
echo "Disk:      $QNX_DISK"
echo "Build dir: $BUILD_DIR"
echo "Guest dir: $GUEST_BUILD_DIR"
echo "Serial:    127.0.0.1:$SERIAL_PORT"
echo "Log:       $HOST_LOG"
if [[ -n "$QNX_CMD" ]]; then
  echo "Command:   $QNX_CMD"
else
  echo "Command:   (none — boot only)"
fi

echo "=== Starting QEMU ==="
if [[ "$(basename "$QNX_DISK")" == "disk-qemu" ]]; then
  QEMU_DISK_ARGS=(-drive "file=$QNX_DISK,format=raw,if=ide,id=drv0")
else
  QEMU_DISK_ARGS=(-drive "file=$QNX_DISK,if=ide,id=drv0")
fi

QEMU_ARGS=(
  --enable-kvm
  "${QEMU_DISK_ARGS[@]}"
  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no
  -device virtio-net-pci,netdev=net0
  -kernel "$QEMU_DIR/output/ifs.bin"
  -nographic
  -monitor none
  -serial tcp:127.0.0.1:$SERIAL_PORT,server,nowait
  --cpu host,host-phys-bits-limit=40
  -smp 4 -m 4G
)

if [[ "$KEEP_QEMU" == 1 ]]; then
  setsid qemu-system-x86_64 "${QEMU_ARGS[@]}" </dev/null >"$BOOT_LOG" 2>&1 &
else
  qemu-system-x86_64 "${QEMU_ARGS[@]}" </dev/null >"$BOOT_LOG" 2>&1 &
fi
QEMU_PID=$!
if [[ "$KEEP_QEMU" == 1 ]]; then
  disown "$QEMU_PID" 2>/dev/null || true
fi
echo "QEMU PID: $QEMU_PID"

export QNX_SERIAL_PORT="$SERIAL_PORT"
export QNX_BOOT_TIMEOUT="$BOOT_TIMEOUT"
export QNX_CMD_TIMEOUT="$CMD_TIMEOUT"
export QNX_COMMAND="$QNX_CMD"
export QNX_HOST_LOG="$HOST_LOG"
export QNX_BOOT_LOG="$BOOT_LOG"
export QNX_SERIAL_LOG="$SERIAL_LOG"
export QNX_KEEP_QEMU="$KEEP_QEMU"
export QNX_GUEST_BUILD_DIR="$GUEST_BUILD_DIR"
export QNX_GUEST_MAIN_BINARY="$GUEST_MAIN_BINARY"
export QNX_EXTRA_ENV="${EXTRA_ENV[*]:+${EXTRA_ENV[*]}}"

python3 - <<'PY'
import os, re, socket, sys, time

port = int(os.environ['QNX_SERIAL_PORT'])
boot_timeout = float(os.environ['QNX_BOOT_TIMEOUT'])
cmd_timeout = float(os.environ['QNX_CMD_TIMEOUT'])
command = os.environ.get('QNX_COMMAND', '')
host_log = os.environ['QNX_HOST_LOG']
boot_log = os.environ['QNX_BOOT_LOG']
serial_log = os.environ['QNX_SERIAL_LOG']
guest_build_dir = os.environ['QNX_GUEST_BUILD_DIR']
guest_main_binary = os.environ['QNX_GUEST_MAIN_BINARY']
extra_env = os.environ.get('QNX_EXTRA_ENV', '')
READY = '__PI_QNX_READY__'
EXIT_RE = re.compile(rb'__PI_QNX_EXIT__:(\d+)')

def q(s):
    return "'" + s.replace("'", "'\\''") + "'"

def connect():
    deadline = time.time() + boot_timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=2.0)
            s.setblocking(False)
            return s
        except OSError:
            time.sleep(1)
    raise TimeoutError(f'timed out connecting to serial port {port}')

def send_line(sock, line):
    sock.sendall((line + '\n').encode())

def looks_like_shell_prompt(buf):
    tail = buf[-800:]
    return bool(re.search(rb'(?m)(^|[\r\n])[^\r\n]{0,100}#\s*$', tail) or
                re.search(rb'(?m)(^|[\r\n])[^\r\n]{0,100}\$.*#?\s*$', tail))

def wait_prompt(sock, timeout=30):
    buf = b''
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            data = sock.recv(4096)
        except (BlockingIOError, socket.timeout):
            data = b''
        if data:
            buf += data
            buf = buf[-8192:]
            log_live(data)
            if READY.encode() in buf or looks_like_shell_prompt(buf):
                return True
        time.sleep(0.05)
    return False

setup_lines = [
    'umount /mnt/nfs 2>/dev/null',
    'umount /mnt 2>/dev/null',
    'mkdir -p /mnt',
    'ifconfig vtnet0 10.0.2.2 netmask 255.255.255.0 up',
    'route add default 10.0.2.1',
    'mkdir -p /mnt/nfs',
    'fs-nfs3 10.0.2.1:/export/chromium-src /mnt/nfs',
    f'cd {guest_build_dir}',
]

env_lines = [
    f'export LD_LIBRARY_PATH={guest_build_dir}',
    f'export CHROME_EXE_PATH={guest_build_dir}/{guest_main_binary}',
    'export CR_SOURCE_ROOT=/mnt/nfs',
]
if extra_env:
    for envvar in extra_env.split():
        env_lines.append(f'export {envvar}')

boot_fp = open(boot_log, 'ab', buffering=0)
serial_fp = open(serial_log, 'ab', buffering=0)

def log_boot(data):
    boot_fp.write(data)

def log_live(data):
    serial_fp.write(data)
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()

sock = connect()
print('=== Connected to QNX serial; waiting for login/shell ===')
sys.stdout.flush()

buf = b''
login_attempts = 0
login_sent = False
password_sent = False
last_wakeup = 0.0
ready = False
deadline = time.time() + boot_timeout
send_line(sock, '')

while time.time() < deadline and not ready:
    try:
        data = sock.recv(4096)
    except (BlockingIOError, socket.timeout):
        data = b''
    if data:
        buf += data
        buf = buf[-8192:]
        log_boot(data)
        text_low = buf.lower()
        if READY.encode() in buf:
          ready = True
          break
        if b'login incorrect' in text_low:
            login_sent = False
            password_sent = False
            buf = b''
            time.sleep(1)
            continue
        if looks_like_shell_prompt(buf):
            send_line(sock, f'echo {READY}')
            buf = b''
            continue
        if b'password:' in text_low and not password_sent:
            send_line(sock, 'root')
            password_sent = True
            login_sent = True
            buf = b''
            continue
        if b'login:' in text_low and not login_sent:
            if login_attempts >= 3:
                raise TimeoutError('QNX login rejected root credentials 3 times')
            send_line(sock, 'root')
            login_attempts += 1
            login_sent = True
            buf = b''
            continue
    now = time.time()
    if now - last_wakeup > 10 and not login_sent:
        send_line(sock, '')
        last_wakeup = now
    time.sleep(0.05)

if not ready:
    raise TimeoutError('QNX login/shell did not become ready before timeout')

print('\n=== QNX shell ready; running setup ===')
sys.stdout.flush()
for line in setup_lines:
    send_line(sock, line)
    time.sleep(0.05)
    if not wait_prompt(sock, timeout=30):
        print(f'WARNING: no prompt after: {line}')
for line in env_lines:
    send_line(sock, line)
    time.sleep(0.05)
    if not wait_prompt(sock, timeout=15):
        print(f'WARNING: no prompt after: {line}')

if command:
    print('\n=== Running command ===')
    sys.stdout.flush()
    send_line(sock, 'sh -c ' + q(command) + '; echo __PI_QNX_EXIT__:$?')
    buf = b''
    exit_code = None
    deadline = time.time() + cmd_timeout
    while time.time() < deadline:
        try:
            data = sock.recv(4096)
        except (BlockingIOError, socket.timeout):
            data = b''
        if data:
            buf += data
            buf = buf[-8192:]
            log_live(data)
            m = EXIT_RE.search(buf)
            if m:
                exit_code = int(m.group(1))
                break
        time.sleep(0.05)
    if exit_code is None:
        raise TimeoutError(f'QNX command timed out after {cmd_timeout} seconds')
    print(f'\n=== QNX command exit: {exit_code} ===')
else:
    exit_code = 0

print(f'Log: {host_log}')
sys.exit(exit_code)
PY

echo "=== Done ==="
echo "Log: $HOST_LOG"
echo "Serial transcript: $SERIAL_LOG"
echo "Boot log: $BOOT_LOG"
if [[ "$KEEP_QEMU" == 1 || -z "$QNX_CMD" ]]; then
  echo "QEMU kept running: PID=$QEMU_PID serial=tcp://127.0.0.1:$SERIAL_PORT"
  echo "Attach serial: socat -,raw,echo=0 TCP:127.0.0.1:$SERIAL_PORT"
  echo "Stop QEMU: kill $QEMU_PID"
fi
