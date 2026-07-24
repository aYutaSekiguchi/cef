#!/usr/bin/env bash
# Run an arbitrary command on QNX inside full-system QEMU.
# Boots QEMU, logs in on the serial console, mounts the Chromium tree via NFS
# (legacy mode) or downloads a tar payload over rootless passt, then
# changes into the requested build directory, exports runtime env vars,
# and streams the command output back to the host in real time.

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
SHOW_EGL_WARNINGS=0
QEMU_GRAPHICS="${QEMU_GRAPHICS:-headless}"
QEMU_DISPLAY_BACKEND="${QEMU_DISPLAY_BACKEND:-gtk}"
GUI_MODE=0

# Rootless defaults. The legacy TAP+NFS path is kept for compatibility
# and selected by --net-backend=tap --payload-mode=nfs. The rootless
# default is passt (QEMU 10.1+ spawns passt itself, no sudo) plus a
# HTTP-served tar.gz payload (avoids the QEMU 2nd-IDE mount issues
# observed with QNX6/ext2/FAT images on this QNX 8 image).
NET_BACKEND="${NET_BACKEND:-passt}"   # tap | passt
PAYLOAD_MODE="${PAYLOAD_MODE:-http}"   # nfs | http
PASST_ADDRESS="${PASST_ADDRESS:-}"     # guest IPv4 (auto from passt if empty)
PASST_GATEWAY="${PASST_GATEWAY:-}"     # guest default route
PASST_NETMASK="${PASST_NETMASK:-}"     # guest netmask
# HTTP payload mode settings. We bind the host python3 http.server on
# the next free port in [18080, 18100]; the guest fetches the tarball
# via passt's host IP (default 192.168.0.1) and extracts into /data.
HTTP_PAYLOAD_IMAGE="${HTTP_PAYLOAD_IMAGE:-}"    # output tar.gz path
HTTP_PAYLOAD_SRC="${HTTP_PAYLOAD_SRC:-$BUILD_DIR}"  # src dir for manifest
HTTP_PAYLOAD_PORT="${HTTP_PAYLOAD_PORT:-}"        # 0 = pick next free
HTTP_PAYLOAD_PREFIX="${HTTP_PAYLOAD_PREFIX:-payload}"  # tar internal dir
declare -a HTTP_PAYLOAD_EXTRA=()                   # extra --http-payload-file
HTTP_PAYLOAD_MANIFEST="${HTTP_PAYLOAD_MANIFEST:-}" # auto-derived when empty

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
  $0 --virgl --preload-system-egl --kill-existing --dns-server 8.8.8.8 -- \\
      ./cefsimple --ozone-platform=qnx --use-gl=egl --use-native \\
      --use-cmd-decoder=validating --no-sandbox --start-maximized \\
      --url=https://www.youtube.com
  $0 --gui --preload-system-egl --kill-existing --dns-server 8.8.8.8 \\
      --detach --qconn-port 8000 -- ./cefsimple --ozone-platform=qnx \\
      --use-gl=egl --use-native --use-cmd-decoder=validating \\
      --no-sandbox --start-maximized --url=about:blank

CEF runtime options (passed after "--"; not added automatically):
  --ozone-platform=qnx --use-gl=egl --use-native --no-sandbox
      Select the QNX native EGL path used by the validated configuration.
  --use-cmd-decoder=validating
      Explicitly select the validating GLES command decoder. It is the default
      in the patched QNX build, but spelling it out protects launch commands
      from an older build whose passthrough decoder can crash the GPU process.
  --start-maximized
      Match the browser window to the detected QNX Screen display. Recommended
      for full-size rendering; it is not a TLS or GPU-crash workaround.
  --disable-features=HeapProfilerReporting
      Legacy workaround for QNX binaries built before the permanent TLS fix.
      Current patched builds disable heap-profile collection on QNX internally,
      so this switch is no longer required.

Runner options commonly needed by cefsimple:
  --virgl --preload-system-egl
      Provide the virtio-vga-gl display and QNX system Mesa EGL implementation.
  --dns-server 8.8.8.8
      Use an explicit resolver when host DNS discovery selects an unreachable
      server. This was required in the validated YouTube QEMU configuration.

Behavior:
  - mounts /export/chromium-src at /mnt/nfs in the guest
  - changes directory to the BUILD_DIR-relative path under /mnt/nfs
  - exports LD_LIBRARY_PATH, CHROME_EXE_PATH, CR_SOURCE_ROOT

Options:
  --timeout SEC        Command timeout after shell is ready (default: $CMD_TIMEOUT)
  --boot-timeout SEC   Boot/login timeout (default: $BOOT_TIMEOUT)
  --serial-port PORT   TCP serial port (default: $SERIAL_PORT)
  --keep-qemu          Leave QEMU running after the command finishes
  --mount-only         Boot QNX, prepare the selected payload, and leave QEMU running
  --kill-existing      Kill any stale qemu-system-x86_64 first
  --qemu-graphics MODE QEMU display mode: headless, window, or virgl
                       (default: $QEMU_GRAPHICS)
  --virgl              Alias for --qemu-graphics virgl
  --qemu-display NAME  QEMU display backend for window/virgl modes
                       (default: $QEMU_DISPLAY_BACKEND; e.g. gtk, sdl)
  --gui                Alias for virgl + --with-input.  Use with
                       --detach/--keep-qemu for agent-driven GUI tests.
  --preload-system-egl Preload QNX system EGL (/usr/lib/libEGL.so.1)
  --show-egl-warnings Show Mesa EGL warning messages for debugging
                       (default: suppress warnings with EGL_LOG_LEVEL=fatal)
  --env NAME=VALUE     Extra guest environment variable (may repeat)
  --dns-server IP      Guest resolver address; defaults to host DNS discovery
                       (non-loopback IPv4/IPv6 literal). Also used as the
                       passt-side resolver in --net-backend=passt mode.
  --net-backend MODE   Network backend: passt (rootless default) or tap
                       (legacy; needs sudo qnx_setup_env.sh)
  --payload-mode MODE  Payload delivery: http (rootless default; builds a
                       tar.gz and serves it via python3 -m http.server
                       from the host, fetched in the guest over passt)
                       or nfs (legacy; needs sudo)
  --passt-address IP   passt: guest IPv4 (defaults to passt-advertised)
  --passt-gateway IP   passt: guest default route
  --passt-netmask MASK passt: guest netmask (e.g. 255.255.255.0 or /24)
  --http-payload-image  PATH  http mode: payload tar.gz path
                          (default: $BUILD_DIR/qnx_payload.tar.gz)
  --http-payload-src    DIR   http mode: source dir for manifest
                          (default: $BUILD_DIR)
  --http-payload-file   P     http mode: extra host file to add (repeatable)
  --http-payload-manifest NAME http mode: preset manifest; auto-derived
                          from the first command token (cefsimple =>
                          cefsimple manifest; anything else => single
                          binary payload). Pass explicitly to override.
  --http-payload-port   PORT  http mode: port to bind (0 = pick first
                          free port in [18080, 18100]).
  --with-input         Add -device virtio-tablet-pci and a unix QMP socket
                       ($QNX_QMP_SOCK, default $QNX_QMP_SOCK) so a host
                       tool can inject SCREEN_EVENT_POINTER via
                       input-send-event. Off by default; failures are
                       non-fatal and the render-only path is preserved.
  --qmp-socket PATH    Override the QMP Unix socket used by --with-input.
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
  - passt, python3, curl/tar in the guest (rootless default)
  - sudo ${SCRIPT_DIR}/qnx_setup_env.sh (only for --net-backend=tap
    --payload-mode=nfs, the legacy path)
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
    --gui)
      GUI_MODE=1
      QEMU_GRAPHICS="virgl"
      WITH_INPUT=1
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
    --show-egl-warnings)
      SHOW_EGL_WARNINGS=1
      shift
      ;;
    --with-input)
      WITH_INPUT=1
      shift
      ;;
    --qmp-socket)
      QNX_QMP_SOCK="${2:?--qmp-socket requires a path}"
      shift 2
      ;;
    --qmp-socket=*)
      QNX_QMP_SOCK="${1#*=}"
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
    --net-backend)
      NET_BACKEND="${2:?--net-backend requires tap or passt}"
      shift 2
      ;;
    --net-backend=*)
      NET_BACKEND="${1#*=}"
      shift
      ;;
    --payload-mode)
      PAYLOAD_MODE="${2:?--payload-mode requires nfs or http}"
      shift 2
      ;;
    --payload-mode=*)
      PAYLOAD_MODE="${1#*=}"
      shift
      ;;
    --passt-address)
      PASST_ADDRESS="${2:?--passt-address requires an IP}"
      shift 2
      ;;
    --passt-gateway)
      PASST_GATEWAY="${2:?--passt-gateway requires an IP}"
      shift 2
      ;;
    --passt-netmask)
      PASST_NETMASK="${2:?--passt-netmask requires a mask}"
      shift 2
      ;;
    --http-payload-image)
      HTTP_PAYLOAD_IMAGE="${2:?--http-payload-image requires a path}"
      shift 2
      ;;
    --http-payload-src)
      HTTP_PAYLOAD_SRC="${2:?--http-payload-src requires a directory}"
      shift 2
      ;;
    --http-payload-file)
      HTTP_PAYLOAD_EXTRA+=("${2:?--http-payload-file requires a path}")
      shift 2
      ;;
    --http-payload-manifest)
      HTTP_PAYLOAD_MANIFEST="${2:?--http-payload-manifest requires a name}"
      shift 2
      ;;
    --http-payload-port)
      HTTP_PAYLOAD_PORT="${2:?--http-payload-port requires a port number}"
      if ! [[ "$HTTP_PAYLOAD_PORT" =~ ^[0-9]+$ ]] || (( HTTP_PAYLOAD_PORT < 0 || HTTP_PAYLOAD_PORT > 65535 )); then
        echo "ERROR: --http-payload-port must be 0..65535" >&2; exit 2
      fi
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

# QNX Mesa emits a warning for every loader-private EGLImage state it cleans
# up. Keep routine logs readable by default while retaining an explicit debug
# mode. An explicit --env EGL_LOG_LEVEL=... remains the advanced override.
HAS_EGL_LOG_LEVEL=0
for envvar in "${EXTRA_ENV[@]}"; do
  if [[ "$envvar" == EGL_LOG_LEVEL=* ]]; then
    HAS_EGL_LOG_LEVEL=1
  fi
done
if [[ "$SHOW_EGL_WARNINGS" == 1 && "$HAS_EGL_LOG_LEVEL" == 1 ]]; then
  echo "ERROR: --show-egl-warnings cannot be combined with --env EGL_LOG_LEVEL=..." >&2
  exit 2
fi
if [[ "$SHOW_EGL_WARNINGS" == 1 ]]; then
  EXTRA_ENV+=("EGL_LOG_LEVEL=warning")
elif [[ "$HAS_EGL_LOG_LEVEL" == 0 ]]; then
  EXTRA_ENV+=("EGL_LOG_LEVEL=fatal")
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

case "$NET_BACKEND" in
  tap|passt) ;;
  *) echo "ERROR: --net-backend must be 'tap' or 'passt' (got: $NET_BACKEND)" >&2; exit 2 ;;
esac

case "$PAYLOAD_MODE" in
  nfs|http) ;;
  *) echo "ERROR: --payload-mode must be 'nfs' or 'http' (got: $PAYLOAD_MODE)" >&2; exit 2 ;;
esac

if [[ "$MOUNT_ONLY" == 1 ]]; then
  QNX_CMD="true"
elif [[ ${#POSITIONAL[@]} -gt 0 ]]; then
  QNX_CMD="${POSITIONAL[*]}"
else
  QNX_CMD=""
fi

# Boot-only and mount-only HTTP sessions still need a non-empty archive.
if [[ "$PAYLOAD_MODE" == http && -z "$HTTP_PAYLOAD_MANIFEST" && ${#HTTP_PAYLOAD_EXTRA[@]} -eq 0 && ( -z "$QNX_CMD" || "$QNX_CMD" == "true" ) ]]; then
  HTTP_PAYLOAD_MANIFEST="common"
fi

# HTTP payload auto-selection. When the user did not pick a manifest
# explicitly, decide based on the first command token so that
#   ./qnx_run.sh -- ./cefsimple ...
# pulls in libcef.so + resources, but
#   ./qnx_run.sh -- ./base_unittests ...
# only ships base_unittests (no 1.5 GiB libcef.so). Explicit
# --http-payload-manifest overrides this logic. If the user already passed
# --http-payload-file, do not auto-add: the user is supplying their own
# payload and the auto-add would mix in the host BUILD_DIR path which
# may not exist.
if [[ "$PAYLOAD_MODE" == http && -z "$HTTP_PAYLOAD_MANIFEST" && ${#HTTP_PAYLOAD_EXTRA[@]} -eq 0 && -n "$QNX_CMD" && "$QNX_CMD" != "true" ]]; then
  first_token="${QNX_CMD%% *}"
  first_token="${first_token##*/}"
  case "$first_token" in
    cefsimple) HTTP_PAYLOAD_MANIFEST="cefsimple" ;;
    *)         HTTP_PAYLOAD_MANIFEST="common"
               HTTP_PAYLOAD_EXTRA+=("$HTTP_PAYLOAD_SRC/$first_token") ;;
  esac
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
check_file "$BUILD_DIR"

# Backend- and mode-specific precondition checks. The rootless
# passt+http path does not need tap0 or /export/chromium-src; only the
# legacy tap+nfs path does.
if [[ "$NET_BACKEND" == tap ]]; then
  ip link show tap0 >/dev/null 2>&1 || {
    echo "ERROR: tap0 not found. Run: sudo ${SCRIPT_DIR}/qnx_setup_env.sh" >&2
    exit 1
  }
fi
if [[ "$PAYLOAD_MODE" == nfs ]]; then
  check_file /export/chromium-src
fi
if [[ "$NET_BACKEND" == passt ]]; then
  command -v passt >/dev/null 2>&1 || {
    echo "ERROR: passt not found. Install passt (apt: passt)." >&2
    exit 1
  }
fi
if [[ "$PAYLOAD_MODE" == http ]]; then
  "${SCRIPT_DIR}/qnx_payload_http.sh" --help >/dev/null 2>&1 || {
    echo "ERROR: qnx_payload_http.sh is not executable" >&2
    exit 1
  }
fi

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
# nfs mode: guest path is /mnt/nfs/<build_rel>.
# http mode: guest path is /data/qnx_payload/payload (no build subpath).
if [[ "$PAYLOAD_MODE" == http ]]; then
  GUEST_BUILD_DIR="/data/qnx_payload/$HTTP_PAYLOAD_PREFIX"
else
  GUEST_BUILD_DIR="/mnt/nfs/$BUILD_DIR_REL"
fi

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
VERIFY_HTTP_PAYLOAD_BINARY=0
if [[ -n "$QNX_CMD" ]]; then
  first_token="${QNX_CMD%% *}"
  first_token="${first_token%%;*}"
  first_token="${first_token##*/}"
  if [[ -n "$first_token" ]]; then
    GUEST_MAIN_BINARY="$first_token"
    # Only preflight commands expected to come from the payload.  Explicit
    # manifest users may run guest-provided commands such as ifconfig/curl;
    # those must be resolved by the guest PATH, not found in the archive.
    if [[ "$PAYLOAD_MODE" == http && ( -e "$HTTP_PAYLOAD_SRC/$first_token" ||
          ( "$HTTP_PAYLOAD_MANIFEST" == cefsimple && "$first_token" == cefsimple ) ) ]]; then
      VERIFY_HTTP_PAYLOAD_BINARY=1
    fi
  fi
fi

cleanup() {
  status=$?
  if [[ -n "${QEMU_PID:-}" && "$KEEP_QEMU" != 1 ]]; then
    kill "$QEMU_PID" 2>/dev/null || true
    wait "$QEMU_PID" 2>/dev/null || true
  fi
  if [[ -n "${HTTP_SERVER_PID:-}" && "$KEEP_QEMU" != 1 ]]; then
    kill "$HTTP_SERVER_PID" 2>/dev/null || true
    wait "$HTTP_SERVER_PID" 2>/dev/null || true
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
if [[ "$GUI_MODE" == 1 ]]; then
  echo "GUI input: enabled"
  echo "QMP:       $QNX_QMP_SOCK"
elif [[ "$WITH_INPUT" == 1 ]]; then
  echo "QMP:       $QNX_QMP_SOCK"
fi
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

# Build and serve the tar payload. QEMU's passt backend exposes the host at
# 192.168.0.1 by default, so no privileged listener or TAP setup is needed.
if [[ "$PAYLOAD_MODE" == http ]]; then
  [[ -z "$HTTP_PAYLOAD_IMAGE" ]] && HTTP_PAYLOAD_IMAGE="$BUILD_DIR/qnx_payload.tar.gz"
  HTTP_HELPER_ARGS=(--src "$HTTP_PAYLOAD_SRC" --out "$HTTP_PAYLOAD_IMAGE" --prefix "$HTTP_PAYLOAD_PREFIX")
  [[ -n "$HTTP_PAYLOAD_MANIFEST" ]] && HTTP_HELPER_ARGS+=(--manifest "$HTTP_PAYLOAD_MANIFEST")
  for p in "${HTTP_PAYLOAD_EXTRA[@]}"; do
    HTTP_HELPER_ARGS+=(--payload-file "$p")
  done
  echo "--- building HTTP payload ---"
  "${SCRIPT_DIR}/qnx_payload_http.sh" "${HTTP_HELPER_ARGS[@]}"
  if [[ -z "$HTTP_PAYLOAD_PORT" || "$HTTP_PAYLOAD_PORT" == 0 ]]; then
    HTTP_PAYLOAD_PORT="$(python3 - <<'PY'
import socket
for port in range(18080, 18101):
    with socket.socket() as s:
        try:
            s.bind(('0.0.0.0', port))
        except OSError:
            continue
        print(port)
        break
else:
    raise SystemExit('no free HTTP payload port in 18080..18100')
PY
)"
  fi
  HTTP_SERVER_LOG="${BOOT_LOG%.log}_http.log"
  python3 -m http.server "$HTTP_PAYLOAD_PORT" --bind 0.0.0.0 \
    --directory "$(dirname "$HTTP_PAYLOAD_IMAGE")" >"$HTTP_SERVER_LOG" 2>&1 &
  HTTP_SERVER_PID=$!
  sleep 0.2
  kill -0 "$HTTP_SERVER_PID" 2>/dev/null || {
    echo "ERROR: payload HTTP server failed; see $HTTP_SERVER_LOG" >&2
    exit 1
  }
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

# Network device args: passt (rootless, QEMU 10.1+ spawns passt itself)
# vs tap (legacy). The passt -netdev must be specified exactly once;
# all options are assembled into a single comma-separated string.
case "$NET_BACKEND" in
  tap)
    QEMU_NET_ARGS=(-netdev tap,id=net0,ifname=tap0,script=no,downscript=no
                   -device virtio-net-pci,netdev=net0)
    ;;
  passt)
    PASST_OPTS="id=net0,ipv4=on,ipv6=off,quiet=off"
    [[ -n "$PASST_ADDRESS" ]] && PASST_OPTS="${PASST_OPTS},address=${PASST_ADDRESS}"
    [[ -n "$PASST_GATEWAY" ]] && PASST_OPTS="${PASST_OPTS},gateway=${PASST_GATEWAY}"
    [[ -n "$PASST_NETMASK" ]] && PASST_OPTS="${PASST_OPTS},netmask=${PASST_NETMASK}"
    [[ -n "$DNS_SERVER"     ]] && PASST_OPTS="${PASST_OPTS},dns=${DNS_SERVER}"
    QEMU_NET_ARGS=(-netdev "passt,${PASST_OPTS}"
                   -device virtio-net-pci,netdev=net0)
    ;;
esac

QEMU_ARGS=(
  --enable-kvm
  "${QEMU_DISK_ARGS[@]}"
  "${QEMU_NET_ARGS[@]}"
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
export QNX_VERIFY_HTTP_PAYLOAD_BINARY="$VERIFY_HTTP_PAYLOAD_BINARY"
export QNX_EXTRA_ENV="${EXTRA_ENV[*]:+${EXTRA_ENV[*]}}"
export QNX_DETACH="$DETACH"
export QNX_QCONN_PORT="$QCONN_PORT"
export QNX_DNS_SERVER="$DNS_SERVER"
export QNX_NET_BACKEND="$NET_BACKEND"
export QNX_PAYLOAD_MODE="$PAYLOAD_MODE"
export QNX_PASST_ADDRESS="$PASST_ADDRESS"
export QNX_PASST_GATEWAY="$PASST_GATEWAY"
export QNX_PASST_NETMASK="$PASST_NETMASK"
if [[ "$PAYLOAD_MODE" == http ]]; then
  export QNX_HTTP_PAYLOAD_URL="http://${PASST_GATEWAY:-192.168.0.1}:${HTTP_PAYLOAD_PORT}/$(basename "$HTTP_PAYLOAD_IMAGE")"
  export QNX_GUEST_PAYLOAD_DIR="$GUEST_BUILD_DIR"
fi
# --detach: pass BOTH host-side path (where the host tails the file) and
# guest-side path (where the in-guest shell writes via `>`). The host
# path is always the on-host BUILD_DIR. The guest path is
#   - /data/qnx_payload/<prefix>/<log> for http mode
#   - /mnt/nfs/<build_rel>/<log> for nfs mode (writable NFS mount)
HOST_DETACH_APP_LOG="$BUILD_DIR/${RESULT_NAME%.log}_app.log"
HOST_DETACH_QCONN_LOG="$BUILD_DIR/${RESULT_NAME%.log}_qconn.log"
GUEST_DETACH_APP_LOG="$GUEST_BUILD_DIR/${RESULT_NAME%.log}_app.log"
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
net_backend = os.environ.get('QNX_NET_BACKEND', 'passt')
payload_mode = os.environ.get('QNX_PAYLOAD_MODE', 'http')
passt_address = os.environ.get('QNX_PASST_ADDRESS', '')
passt_gateway = os.environ.get('QNX_PASST_GATEWAY', '')
passt_netmask = os.environ.get('QNX_PASST_NETMASK', '')
guest_payload_dir = os.environ.get('QNX_GUEST_PAYLOAD_DIR', '/data/qnx_payload/payload')
http_payload_url = os.environ.get('QNX_HTTP_PAYLOAD_URL', '')
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
    'umount /tmp/qnx_payload 2>/dev/null',
    'umount /mnt/nfs 2>/dev/null',
]
# Network setup. With the passt backend the integrated QEMU passt hands
# the guest an IPv4 lease via DHCP as soon as vtnet0 is up, so we just
# poll for it (bounded). Static --passt-* overrides skip DHCP and apply
# the values directly. The tap backend uses the legacy 10.0.2.x.
# Setup commands NEVER use exit/logout; failure modes write a
# QNX_SETUP_FAIL=<reason> marker that the Python runner inspects.
if net_backend == 'tap':
    setup_lines += [
        'ifconfig vtnet0 10.0.2.2 netmask 255.255.255.0 up',
        'route add default 10.0.2.1',
    ]
elif passt_address:
    setup_lines += [
        f'ifconfig vtnet0 {q(passt_address)} netmask {q(passt_netmask or "255.255.255.0")} up',
    ]
    if passt_gateway:
        setup_lines += [f'route add default {q(passt_gateway)}']
else:
    # Wait up to 60s for passt's DHCP lease to land on vtnet0. We
    # grep for an IPv4 "inet <digit>" line, which excludes inet6
    # (the link-local fe80 line uses inet6). On failure we dump
    # the full ifconfig so the operator can see the state and write
    # a marker. The pattern is intentionally a simple substring
    # match (no POSIX class, no ^ anchor) to keep the shell-quoted
    # form predictable across bash versions.
    setup_lines += [
        'i=0; while [ $i -lt 120 ]; do '
        'ifconfig vtnet0 2>/dev/null | grep -q "inet [0-9]" && break; '
        'sleep 0.5; i=$((i+1)); done',
        'if ! ifconfig vtnet0 2>/dev/null | grep -q "inet [0-9]"; then '
        'echo "QNX_SETUP_DBG_NO_IPV4:"; ifconfig vtnet0 2>&1; '
        'echo "QNX_SETUP_FAIL=passt-dhcp-no-ipv4-after-60s"; fi',
    ]
if payload_mode == 'nfs':
    setup_lines += [
        'mkdir -p /mnt/nfs',
        'fs-nfs3 10.0.2.1:/export/chromium-src /mnt/nfs',
    ]
else:  # http
    # /data is a large writable QNX6 partition. Fetching from passt's host
    # address avoids TAP, NFS exports, and all second-disk filesystem issues.
    setup_lines += [
        'mkdir -p /data/qnx_payload',
        f'rm -rf {q(guest_payload_dir)}',
        f'curl -fsS --connect-timeout 15 --max-time 1800 -o /data/qnx_payload/payload.tar.gz {q(http_payload_url)} '
        '|| echo "QNX_SETUP_FAIL=http-payload-download"',
        'cd /data/qnx_payload',
        'tar -xzf payload.tar.gz || echo "QNX_SETUP_FAIL=http-payload-extract"',
    ]
# The guest build dir is build_dir-relative under the active mount.
# nfs mode: /mnt/nfs/<build_rel>. http mode: /data/qnx_payload/<prefix>.
if payload_mode == 'nfs':
    setup_lines += [f'cd {guest_build_dir}']
    env_setup_root = '/mnt/nfs'
else:
    setup_lines += [f'cd {guest_payload_dir}']
    env_setup_root = guest_payload_dir

dns_server = os.environ.get('QNX_DNS_SERVER', '')
if dns_server:
    setup_lines.append(
        f"printf 'nameserver %s\\n' {q(dns_server)} > /etc/resolv.conf"
    )

env_lines = [
    # Accept both `cefsimple` and `./cefsimple`.  Payload auto-selection
    # already treats those spellings identically, so command lookup must do
    # the same after the archive is extracted (and in legacy NFS mode).
    f'export PATH={env_setup_root if payload_mode == "http" else guest_build_dir}:$PATH',
    f'export LD_LIBRARY_PATH={env_setup_root if payload_mode == "http" else guest_build_dir}',
    f'export CHROME_EXE_PATH={env_setup_root if payload_mode == "http" else guest_build_dir}/{guest_main_binary}',
    f'export CR_SOURCE_ROOT={env_setup_root if payload_mode == "http" else "/mnt/nfs"}',
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

def wait_until_marker(sock, marker, timeout):
    """Block up to `timeout` seconds waiting for `marker` to appear in
    the guest's serial output. Every byte received is forwarded to
    log_live so the operator sees the output in real time. Returns
    the captured bytes on hit, or None on timeout. The caller is
    responsible for scanning the returned bytes for failure markers
    such as `QNX_SETUP_FAIL=...`.
    """
    end = time.time() + timeout
    buf = b''
    while time.time() < end:
        try:
            data = sock.recv(4096)
        except (BlockingIOError, socket.timeout):
            data = b''
        if data:
            log_live(data)
            buf += data
            if marker in buf:
                # Drain a tiny bit more so the rest of the line
                # is visible in the log.
                end_quiet = time.time() + 0.3
                while time.time() < end_quiet:
                    try:
                        more = sock.recv(4096)
                    except (BlockingIOError, socket.timeout):
                        more = b''
                    if not more:
                        break
                    log_live(more)
                    buf += more
                return buf
        time.sleep(0.02)
    return None

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
# Run the entire setup as ONE shell invocation joined with ' && '.
# This avoids the race where a fast loop (e.g. the DHCP poll) returns
# a prompt before the next setup_lines element is sent, leaving that
# next element queued in the shell's stdin and counted as part of the
# previous command's "prompt". The trailing marker is required to
# appear on its own line in the output; the runner scans for it and
# also for any QNX_SETUP_FAIL=<reason> marker.
SETUP_DONE_MARKER = b'__QNX_SETUP_DONE__'
SETUP_OUTPUT = b''
if setup_lines:
    # Semicolon-chain setup commands. Critical failures are signalled
    # by the `QNX_SETUP_FAIL=<reason>` marker (written instead of
    # calling exit, which would tear the shell down). Non-zero
    # individual returns (umount of a path that isn't mounted, etc.)
    # are tolerated so the rest of the setup still runs.
    joined = ' ; '.join(setup_lines) + f' ; echo {SETUP_DONE_MARKER.decode()}'
    # The serial tty normally echoes the command line. Since the command
    # itself contains both DONE and FAIL marker text, scanning that echo
    # would produce an immediate false success/failure before execution.
    # Disable tty echo while setup runs so only command output is parsed.
    send_line(sock, 'stty -echo')
    wait_prompt(sock, timeout=5)
    send_line(sock, joined)
    setup_timeout = max(90, cmd_timeout + 120) if payload_mode == 'http' else 90
    SETUP_OUTPUT = wait_until_marker(sock, SETUP_DONE_MARKER, timeout=setup_timeout)
    send_line(sock, 'stty echo')
    wait_prompt(sock, timeout=5)
    if SETUP_OUTPUT is None:
        raise RuntimeError(
            f'qnx_run.sh: setup did not complete within {setup_timeout}s; '
            'check guest setup output above'
        )
    fail = re.search(rb'QNX_SETUP_FAIL=([A-Za-z0-9_.-]+)', SETUP_OUTPUT)
    if fail:
        raise RuntimeError(f'qnx_run.sh: setup failed: {fail.group(1).decode()}')

# In http mode, verify the extracted payload contains the command binary.
# We add this as a separate single-shot command so the FAIL marker
# path above is not coupled to the mount-content check.
if (payload_mode == 'http' and command and command != 'true' and
        os.environ.get('QNX_VERIFY_HTTP_PAYLOAD_BINARY', '0') == '1'):
    first_token = command.split()[0].rsplit('/', 1)[-1]
    if first_token:
        check_done = b'__QNX_PAYLOAD_CHECK_DONE__'
        chk = (
            f'cd {q(guest_payload_dir)} && '
            f'[ -x ./{q(first_token)} ] && echo "QNX_SETUP_OK=payload-{q(first_token)}" '
            f'|| echo "QNX_SETUP_FAIL=payload-missing-{q(first_token)}"; '
            f'echo {check_done.decode()}'
        )
        send_line(sock, 'stty -echo')
        wait_prompt(sock, timeout=5)
        send_line(sock, chk)
        captured = wait_until_marker(sock, check_done, timeout=15)
        send_line(sock, 'stty echo')
        wait_prompt(sock, timeout=5)
        if captured is None or b'QNX_SETUP_FAIL=' in captured:
            raise RuntimeError(
                f'qnx_run.sh: payload at {guest_payload_dir} does not contain '
                f'./{first_token} as an executable (image build skipped it, '
                'wrong src, or executable mode was lost)'
            )
        wait_prompt(sock, timeout=5)

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
  if [[ "$WITH_INPUT" == 1 ]]; then
    echo "QMP socket: $QNX_QMP_SOCK"
    echo "GUI tool: ./cef/tools/qnx_gui.py --socket $QNX_QMP_SOCK --json screenshot --output out/qnx_release/gui.png"
  fi
  echo "Stop QEMU: kill $QEMU_PID"
  if [[ -n "${HTTP_SERVER_PID:-}" ]]; then
    echo "Payload HTTP server: PID=$HTTP_SERVER_PID port=$HTTP_PAYLOAD_PORT"
    echo "Stop payload server: kill $HTTP_SERVER_PID"
  fi
fi
