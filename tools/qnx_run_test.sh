#!/usr/bin/env bash
# Convenience wrapper for running gtest-style binaries on QNX via qnx_run.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHROMIUM_SRC="${CHROMIUM_SRC:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$CHROMIUM_SRC/out/qnx_release}"
RUNNER="$SCRIPT_DIR/qnx_run.sh"

BOOT_TIMEOUT="${BOOT_TIMEOUT:-120}"
TEST_TIMEOUT="${TEST_TIMEOUT:-1800}"
SERIAL_PORT="${SERIAL_PORT:-10024}"
FILTER=""
QNX_CMD=""
BINARY="base_unittests"
KEEP_QEMU=0
KILL_EXISTING=0
MOUNT_ONLY=0
EXTRA_ARGS=()

usage() {
  cat <<EOF
Usage:
  $0 [options] [gtest_filter]

Examples:
  $0 'OutOfMemoryHandledTest.*'
  $0 '-*DeathTest*'
  $0 --timeout 3600 '-*DeathTest*'
  $0 --binary base_unittests 'DeathTest.*'
  $0 --cmd './base_unittests --gtest_list_tests'

Options:
  --filter FILTER      GoogleTest filter. Same as positional FILTER.
  --binary NAME        Binary under BUILD_DIR (default: $BINARY).
  --cmd CMD            Run an arbitrary guest shell command instead of gtest.
  --timeout SEC        Test timeout after shell is ready (default: $TEST_TIMEOUT).
  --boot-timeout SEC   Boot/login timeout (default: $BOOT_TIMEOUT).
  --serial-port PORT   TCP serial port (default: $SERIAL_PORT).
  --keep-qemu          Leave QEMU running after command finishes.
  --mount-only         Boot + mount NFS only, then leave QEMU running.
  --kill-existing      Kill any stale qemu-system-x86_64 first.
  -h, --help           Show help.

Prerequisite:
  sudo $SCRIPT_DIR/qnx_setup_env.sh
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --filter)
      FILTER="${2:?--filter requires a value}"
      shift 2
      ;;
    --binary)
      BINARY="${2:?--binary requires a value}"
      shift 2
      ;;
    --cmd)
      QNX_CMD="${2:?--cmd requires a value}"
      shift 2
      ;;
    --timeout)
      TEST_TIMEOUT="${2:?--timeout requires seconds}"
      shift 2
      ;;
    --boot-timeout)
      BOOT_TIMEOUT="${2:?--boot-timeout requires a value}"
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
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    *)
      if [[ -z "$FILTER" && -z "$QNX_CMD" ]]; then
        FILTER="$1"
        shift
      else
        EXTRA_ARGS+=("$1")
        shift
      fi
      ;;
  esac
done
while [[ $# -gt 0 ]]; do
  EXTRA_ARGS+=("$1")
  shift
done

# Support: qnx_run_test.sh -- '-*DeathTest*'
if [[ -z "$FILTER" && -z "$QNX_CMD" && ${#EXTRA_ARGS[@]} -gt 0 ]]; then
  FILTER="${EXTRA_ARGS[0]}"
  EXTRA_ARGS=("${EXTRA_ARGS[@]:1}")
fi

QNX_ENV_EXCLUSIONS="StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree:ImportantFileWriterTest.FailedWriteWithObserver:*AnyCriticalThreadHung*"

if [[ "$MOUNT_ONLY" == 1 ]]; then
  ARGS=(
    --boot-timeout "$BOOT_TIMEOUT"
    --serial-port "$SERIAL_PORT"
    --mount-only
  )
  [[ "$KEEP_QEMU" == 1 ]] && ARGS+=(--keep-qemu)
  [[ "$KILL_EXISTING" == 1 ]] && ARGS+=(--kill-existing)
  exec env BUILD_DIR="$BUILD_DIR" "$RUNNER" "${ARGS[@]}"
fi

if [[ -z "$FILTER" && -z "$QNX_CMD" ]]; then
  FILTER="*"
fi

if [[ -n "$FILTER" ]]; then
  if [[ "$FILTER" != *":-"* && "$FILTER" != "*" ]]; then
    FILTER="$FILTER:-$QNX_ENV_EXCLUSIONS"
  elif [[ "$FILTER" == "*" ]]; then
    FILTER="*:-$QNX_ENV_EXCLUSIONS"
  fi
fi

if [[ -z "$QNX_CMD" ]]; then
  [[ -e "$BUILD_DIR/$BINARY" ]] || { echo "ERROR: missing $BUILD_DIR/$BINARY" >&2; exit 1; }
  QNX_CMD="./$BINARY"
  if [[ -n "$FILTER" ]]; then
    QNX_CMD+=" --gtest_filter=$FILTER"
  fi
  if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    QNX_CMD+=" ${EXTRA_ARGS[*]}"
  fi
fi

ARGS=(
  --timeout "$TEST_TIMEOUT"
  --boot-timeout "$BOOT_TIMEOUT"
  --serial-port "$SERIAL_PORT"
)
[[ "$KEEP_QEMU" == 1 ]] && ARGS+=(--keep-qemu)
[[ "$KILL_EXISTING" == 1 ]] && ARGS+=(--kill-existing)

exec env BUILD_DIR="$BUILD_DIR" "$RUNNER" "${ARGS[@]}" -- "$QNX_CMD"
