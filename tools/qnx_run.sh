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
PRELOAD_SYSTEM_EGL=0
QEMU_GRAPHICS="${QEMU_GRAPHICS:-headless}"
QEMU_DISPLAY_BACKEND="${QEMU_DISPLAY_BACKEND:-gtk}"

# Phase 5 input-injection helper. Default 0 (no input device / no QMP),
# so render-only flows are unchanged. When set, qnx_run.sh adds
#   -device virtio-tablet-pci
#   -qmp unix:/tmp/qnx-qmp.sock,server,nowait
# to the QEMU command line. Existing socket is removed before launch so
# a stale file from a previous run does not block the bind. Failures here
# are non-fatal: a warning is logged and the launch proceeds without input.
WITH_INPUT=0
QNX_QMP_SOCK="${QNX_QMP_SOCK:-/tmp/qnx-qmp.sock}"
# --detach: run the post-`--` QNX command in the guest shell as a background
# job (stdout/stderr/stdin redirected to a log file in BUILD_DIR), do NOT
# wait for it, exit the wrapper cleanly, and implicitly keep QEMU alive.
# Solves the "serial blocked after keep-qemu" problem: the foreground
# command does not own serial once the wrapper exits.
DETACH=0
# --qconn-port: with --detach, also start `qconn port=$QCONN_PORT` in the
# guest shell as a background job. Reuses an already-listening qconn on
# the same port if present. Default port 8000.
QCONN_PORT=""
# Guest DNS server. When unset, discover the first non-loopback host
# resolver; QNX_DNS_SERVER or --dns-server overrides discovery.
DNS_SERVER="${QNX_DNS_SERVER:-}"

usage() {
  cat <<EOF
Usage: $0 [options] [--] command...

Examples:
  $0 ./base_unittests --gtest_filter=ProcessTest.Create
  $0 -- ./base_unittests --gtest_filter=-*DeathTest*
  $0 --keep-qemu -- bash
  $0 --mount-only
  $0 --qemu-graphics virgl -- egl-configs
  $0 --virgl --preload-system-egl -- ./cefsimple --use-native --use-gl=egl
  $0 --virgl --preload-system-egl --with-input --kill-existing \\
      --detach --qconn-port 8000 -- ./cefsimple --use-gl=egl \\
      --use-native --ozone-platform=qnx --no-sandbox

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
  --qemu-graphics MODE QEMU display mode: headless, window, or virgl
                       (default: $QEMU_GRAPHICS)
  --virgl              Alias for --qemu-graphics virgl
  --qemu-display NAME  QEMU display backend for window/virgl modes
                       (default: $QEMU_DISPLAY_BACKEND; e.g. gtk, sdl)
  --preload-system-egl Preload QNX system EGL (/usr/lib/libEGL.so.1)
  --env NAME=VALUE     Extra guest environment variable (may repeat)
  --dns-server IP      Guest resolver address; defaults to host DNS discovery
                       (non-loopback IPv4/IPv6 literal)
  --with-input         Add -device virtio-tablet-pci and a unix QMP socket
                       ($QNX_QMP_SOCK, default $QNX_QMP_SOCK) so a host
                       tool can inject SCREEN_EVENT_POINTER via
                       input-send-event. Off by default; failures are
                       non-fatal and the render-only path is preserved.
  --detach             Run the post-"--" QNX command in the guest shell as
                       a background job; do not wait for it; exit the
                       wrapper cleanly. Implicitly keeps QEMU alive.
                       App stdout/stderr/stdin are redirected to
                       <HOST_LOG>_app.log in BUILD_DIR. The app PID is
                       printed as APP_PID=<pid>. Requires a command
                       after "--". Incompatible with the legacy
                       foreground/keep-qemu UX.
  --qconn-port PORT    With --detach, start "qconn port=PORT" in the
                       guest shell as a background job. If a qconn is
                       already listening on PORT, the existing one is
                       reused (reported as QCONN_REUSED=1). Default
                       port 8000. Requires --detach.
  -h, --help           Show help

Prerequisite:
  sudo ${SCRIPT_DIR}/qnx_setup_env.sh
EOF
}

check_file() {
  [[ -e "$1" ]] || { echo "ERROR: missing $1" >&2; exit 1; }
}

is_valid_dns_server() {
  local candidate="$1"
  [[ "$candidate" =~ ^[0-9A-Fa-f:.]+$ ]] || return 1
  QNX_DNS_SERVER_CANDIDATE="$candidate" python3 - <<'PY' >/dev/null 2>&1
import ipaddress
import os

address = ipaddress.ip_address(os.environ["QNX_DNS_SERVER_CANDIDATE"])
if (address.is_loopback or address.is_unspecified or
        address.is_multicast or address.is_link_local):
    raise SystemExit(1)
PY
}

validate_dns_server() {
  local candidate="$1"
  if ! is_valid_dns_server "$candidate"; then
    echo "ERROR: --dns-server must be a valid non-loopback IP literal (got: $candidate)" >&2
    exit 2
  fi
}

discover_dns_server() {
  local candidate
  if command -v resolvectl >/dev/null 2>&1; then
    while read -r candidate; do
      if is_valid_dns_server "$candidate"; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done < <(resolvectl dns 2>/dev/null | awk '{ for (i = 1; i <= NF; ++i) if ($i ~ /^[0-9A-Fa-f:.]*[.:][0-9A-Fa-f:.]*$/) print $i }')
  fi
  if [[ -r /etc/resolv.conf ]]; then
    while read -r keyword candidate _; do
      if [[ "$keyword" == nameserver ]] && is_valid_dns_server "$candidate"; then
        printf '%s\n' "$candidate"
        return 0
      fi
    done < /etc/resolv.conf
  fi
  return 1
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
    --qemu-graphics)
      QEMU_GRAPHICS="${2:?--qemu-graphics requires a mode}"
      shift 2
      ;;
    --qemu-graphics=*)
      QEMU_GRAPHICS="${1#*=}"
      shift
      ;;
    --virgl)
      QEMU_GRAPHICS="virgl"
      shift
      ;;
    --qemu-display)
      QEMU_DISPLAY_BACKEND="${2:?--qemu-display requires a backend name}"
      shift 2
      ;;
    --qemu-display=*)
      QEMU_DISPLAY_BACKEND="${1#*=}"
      shift
      ;;
    --preload-system-egl)
      PRELOAD_SYSTEM_EGL=1
      shift
      ;;
    --with-input)
      WITH_INPUT=1
      shift
      ;;
    --detach)
      DETACH=1
      KEEP_QEMU=1   # detach implicitly keeps QEMU alive
      shift
      ;;
    --qconn-port)
      QCONN_PORT="${2:?--qconn-port requires a port number}"
      # Validate: must be a positive integer 1..65535.
      if ! [[ "$QCONN_PORT" =~ ^[0-9]+$ ]] || (( QCONN_PORT < 1 || QCONN_PORT > 65535 )); then
        echo "ERROR: --qconn-port must be an integer 1..65535 (got: $QCONN_PORT)" >&2
        exit 2
      fi
      shift 2
      ;;
    --qconn-port=*)
      QCONN_PORT="${1#*=}"
      if ! [[ "$QCONN_PORT" =~ ^[0-9]+$ ]] || (( QCONN_PORT < 1 || QCONN_PORT > 65535 )); then
        echo "ERROR: --qconn-port must be an integer 1..65535 (got: $QCONN_PORT)" >&2
        exit 2
      fi
      shift
      ;;
    --dns-server)
      DNS_SERVER="${2:?--dns-server requires an IP address}"
      shift 2
      ;;
    --dns-server=*)
      DNS_SERVER="${1#*=}"
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

if [[ -z "$DNS_SERVER" ]]; then
  DNS_SERVER="$(discover_dns_server || true)"
fi
if [[ -n "$DNS_SERVER" ]]; then
  validate_dns_server "$DNS_SERVER"
else
  echo "WARNING: no non-loopback host DNS server found; pass --dns-server IP" >&2
fi

if [[ "$PRELOAD_SYSTEM_EGL" == 1 ]]; then
  for envvar in "${EXTRA_ENV[@]}"; do
    if [[ "$envvar" == LD_PRELOAD=* ]]; then
      echo "ERROR: --preload-system-egl cannot be combined with --env LD_PRELOAD=..." >&2
      exit 2
    fi
  done
  EXTRA_ENV+=("LD_PRELOAD=/usr/lib/libEGL.so.1")
fi

# --detach requires a command; --qconn-port requires --detach
if [[ "$DETACH" == 1 && ${#POSITIONAL[@]} -eq 0 ]]; then
  echo "ERROR: --detach requires a command after --" >&2
  exit 2
fi
if [[ -n "$QCONN_PORT" && "$DETACH" != 1 ]]; then
  echo "ERROR: --qconn-port requires --detach" >&2
  exit 2
fi
if [[ -z "$QCONN_PORT" && "$DETACH" == 1 ]]; then
  # Default port 8000 when --detach is used without explicit --qconn-port.
  QCONN_PORT="8000"
fi

case "$QEMU_GRAPHICS" in
  headless|window|virgl)
    ;;
  *)
    echo "ERROR: unsupported --qemu-graphics mode: $QEMU_GRAPHICS" >&2
    echo "       Supported modes: headless, window, virgl" >&2
    exit 2
    ;;
esac

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
  echo "ERROR: tap0 not found. Run: sudo ${SCRIPT_DIR}/qnx_setup_env.sh" >&2
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
echo "Graphics:  $QEMU_GRAPHICS"
if [[ -n "$DNS_SERVER" ]]; then
  echo "DNS:       $DNS_SERVER"
else
  echo "DNS:       (unset)"
fi
if [[ "$QEMU_GRAPHICS" != "headless" ]]; then
  echo "Display:   $QEMU_DISPLAY_BACKEND"
fi
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

case "$QEMU_GRAPHICS" in
  headless)
    QEMU_GRAPHICS_ARGS=(-nographic)
    ;;
  window)
    QEMU_GRAPHICS_ARGS=(-display "$QEMU_DISPLAY_BACKEND")
    ;;
  virgl)
    QEMU_GRAPHICS_ARGS=(-vga none -device virtio-vga-gl -display "$QEMU_DISPLAY_BACKEND,gl=on")
    ;;
esac

QEMU_ARGS=(
  --enable-kvm
  "${QEMU_DISK_ARGS[@]}"
  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no
  -device virtio-net-pci,netdev=net0
  -kernel "$QEMU_DIR/output/ifs.bin"
  "${QEMU_GRAPHICS_ARGS[@]}"
  -monitor none
  -serial tcp:127.0.0.1:$SERIAL_PORT,server,nowait
  --cpu host,host-phys-bits-limit=40
  -smp 4 -m 4G
)

# Phase 5: optional input-injection wiring. Off by default so render-only
# flows are unchanged. When --with-input is set, append -device
# virtio-tablet-pci so QNX Screen receives pointer events, plus a local
# unix QMP socket so a host tool can drive input-send-event programmatically.
# Failures here (QEMU option parse error, socket cleanup) are non-fatal:
# we log a warning and proceed with the base QEMU_ARGS so the user still
# gets a working render-only boot if the kernel rejects the device.
if [[ "$WITH_INPUT" == 1 ]]; then
  if rm -f "$QNX_QMP_SOCK" 2>/dev/null; then
    :
  fi
  # Probe whether QEMU accepts the input device first by checking device
  # help. If the probe fails, fall back to the base args.
  if qemu-system-x86_64 -device help 2>/dev/null | grep -q 'virtio-tablet-pci'; then
    QEMU_INPUT_ARGS=(-device virtio-tablet-pci -qmp "unix:$QNX_QMP_SOCK,server,nowait")
  else
    echo "WARNING: virtio-tablet-pci not available; --with-input ignored" >&2
    QEMU_INPUT_ARGS=()
  fi
else
  QEMU_INPUT_ARGS=()
fi

if [[ "$KEEP_QEMU" == 1 ]]; then
  setsid qemu-system-x86_64 "${QEMU_ARGS[@]}" "${QEMU_INPUT_ARGS[@]}" </dev/null >"$BOOT_LOG" 2>&1 &
else
  qemu-system-x86_64 "${QEMU_ARGS[@]}" "${QEMU_INPUT_ARGS[@]}" </dev/null >"$BOOT_LOG" 2>&1 &
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
export QNX_DETACH="$DETACH"
export QNX_QCONN_PORT="$QCONN_PORT"
export QNX_DNS_SERVER="$DNS_SERVER"
# --detach: pass BOTH host-side path (where the host tails the file) and
# guest-side path (where the in-guest shell writes via `>`). The guest
# path is on the NFS-mounted BUILD_DIR so the redirect is writable and
# the file is visible to the host. They differ only in the
# /home/yuta/... -> /mnt/nfs/... prefix.
HOST_DETACH_APP_LOG="$BUILD_DIR/${RESULT_NAME%.log}_app.log"
GUEST_DETACH_APP_LOG="$GUEST_BUILD_DIR/${RESULT_NAME%.log}_app.log"
HOST_DETACH_QCONN_LOG="$BUILD_DIR/${RESULT_NAME%.log}_qconn.log"
GUEST_DETACH_QCONN_LOG="$GUEST_BUILD_DIR/${RESULT_NAME%.log}_qconn.log"
export QNX_DETACH_APP_LOG_HOST="$HOST_DETACH_APP_LOG"
export QNX_DETACH_APP_LOG_GUEST="$GUEST_DETACH_APP_LOG"
export QNX_DETACH_QCONN_LOG_HOST="$HOST_DETACH_QCONN_LOG"
export QNX_DETACH_QCONN_LOG_GUEST="$GUEST_DETACH_QCONN_LOG"

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
dns_server = os.environ.get('QNX_DNS_SERVER', '')
if dns_server:
    setup_lines.insert(
        5, f"printf 'nameserver %s\\n' {q(dns_server)} > /etc/resolv.conf"
    )

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
    # --detach: launch the post-`--` QNX command in the guest shell as
    # a background job (stdin/stdout/stderr redirected to a log file on
    # the NFS-mounted guest BUILD_DIR), do NOT wait for it, exit the
    # wrapper cleanly. KEEP_QEMU=1 is set implicitly. The host can tail
    # the same log via the host-side mirror path. We use a SINGLE guest
    # shell command that starts qconn (optional) and the app, captures
    # their background-launch PIDs via $!, and emits three markers:
    # APP_LAUNCH_PID, QCONN_LAUNCH_PID, DETACH_READY. The Python helper
    # waits only for DETACH_READY, then exits. No additional pidin/proc
    # probes are issued (which would risk serial blocking).
    if os.environ.get('QNX_DETACH', '0') == '1':
        print('\n=== --detach mode: launching in background, returning immediately ===')
        sys.stdout.flush()

        qconn_port = os.environ.get('QNX_QCONN_PORT', '').strip()
        guest_app_log = os.environ.get('QNX_DETACH_APP_LOG_GUEST',
                                       '/tmp/qnx-detach-app.log')
        host_app_log = os.environ.get('QNX_DETACH_APP_LOG_HOST',
                                      guest_app_log)
        guest_qconn_log = os.environ.get('QNX_DETACH_QCONN_LOG_GUEST',
                                         '/tmp/qnx-detach-qconn.log')
        host_qconn_log = os.environ.get('QNX_DETACH_QCONN_LOG_HOST',
                                         guest_qconn_log)

        # Launch qconn and the app as TWO separate backgrounded guest
        # shell commands (no `{ ... }` subshell wrapper). Each line uses
        # the pattern `<bg-cmd> & echo NAME_SENT=$!` so the foreground
        # shell echoes a marker immediately (does NOT wait for the
        # backgrounded job). We read each marker from serial, parse the
        # PID, then exit. NO additional pidin/proc probe is issued
        # (which would risk serial blocking). We do NOT claim the PID
        # is the "real cefsimple executable"; we label it APP_LAUNCH_PID
        # (and QCONN_LAUNCH_PID for qconn) — callers can pidin the
        # guest later to map to the actual child.
        env_prefix = '; '.join(env_lines)

        def _send_and_wait_marker(line, marker, timeout=8):
            """send_line, then read from serial until `marker` appears
            (or timeout). Returns the buffer. Follows the existing
            wait_prompt pattern: empty reads and BlockingIOError are
            NOT fatal — we keep polling until deadline so a marker
            emitted slightly after send_line is not missed."""
            send_line(sock, line)
            buf = b''
            ddl = time.time() + timeout
            while time.time() < ddl:
                try:
                    data = sock.recv(4096)
                except (BlockingIOError, socket.timeout):
                    data = b''
                if data:
                    buf += data
                    if marker.encode() in buf:
                        return buf
                time.sleep(0.05)
            return buf

        # Step 1: launch qconn in background, echo QCONN_LAUNCH_SENT=$!
        # immediately. We do NOT detect an existing qconn (no /proc/
        # net/tcp LISTEN parsing — that would be Linux-specific). If a
        # qconn is already on the port the new launch fails silently
        # in the guest log; the existing one keeps serving.
        qconn_launch_pid = '?'
        if qconn_port:
            qconn_line = (
                f'qconn port={qconn_port} > {guest_qconn_log} 2>&1 < /dev/null & '
                f'echo QCONN_LAUNCH_SENT=$!'
            )
            buf = _send_and_wait_marker(qconn_line, 'QCONN_LAUNCH_SENT',
                                        timeout=8)
            sent = b'QCONN_LAUNCH_SENT' in buf
            import re as _re
            m = _re.search(rb'QCONN_LAUNCH_SENT=(\S+)', buf)
            if m:
                qconn_launch_pid = m.group(1).decode()
            print(f'QCONN_PORT={qconn_port} QCONN_LAUNCH_SENT={int(sent)} '
                  f'QCONN_LAUNCH_PID={qconn_launch_pid} '
                  f'QCONN_LOG_GUEST={guest_qconn_log} QCONN_LOG_HOST={host_qconn_log}')
            log_live(
                f'[qnx_run.sh] qconn sent={sent} pid={qconn_launch_pid} port={qconn_port}\n'.encode())

        # Step 2: launch the app in background, echo APP_LAUNCH_SENT=$!
        # immediately. `sh -c {q(command)}` — q() quotes the command
        # for shell embedding — runs cmd as a child of sh, same PID
        # preserved as $!. Backgrounded with `&` so the parent shell
        # returns to prompt without waiting. stdout/stderr/stdin are
        # redirected to the guest-side log on NFS (writable inside the
        # guest, mirror path on host).
        app_line = (
            f'cd {guest_build_dir} && {env_prefix} && '
            f'sh -c {q(command)} > {guest_app_log} 2>&1 < /dev/null & '
            f'echo APP_LAUNCH_SENT=$!'
        )
        buf = _send_and_wait_marker(app_line, 'APP_LAUNCH_SENT', timeout=8)
        sent = b'APP_LAUNCH_SENT' in buf
        app_launch_pid = '?'
        m = _re.search(rb'APP_LAUNCH_SENT=(\S+)', buf)
        if m:
            app_launch_pid = m.group(1).decode()
        print(f'APP_LAUNCH_SENT={int(sent)} '
              f'APP_LAUNCH_PID={app_launch_pid} '
              f'APP_LOG_GUEST={guest_app_log} APP_LOG_HOST={host_app_log}')
        log_live(
            f'[qnx_run.sh] app sent={sent} pid={app_launch_pid}\n'.encode())

        print('Detach complete. Wrapper exiting; QEMU remains running.')
        sys.stdout.flush()
        sys.exit(0)


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
