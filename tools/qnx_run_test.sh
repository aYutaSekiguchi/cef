#!/usr/bin/env bash
# Convenience dispatcher for the QNX QEMU test runner.
#
# Behaviour is preserved for every command documented in
# docs/qnx/testing.md and docs/qnx/fixes-and-decisions.md.  The actual
# implementation now lives under tools/qnx_tests/ and is a Python
# package (one source of truth for serial I/O, login, and per-module
# knowledge).
#
# Recognised module flags: --base, --v8, --swiftshader, --angle,
# --all, --list.
# When no module flag is given, the default is --base (preserves the
# historical "run base_unittests" UX of the old qnx_run_test.sh).
#
# See tools/qnx_tests/cli.py for the canonical help text.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$SCRIPT_DIR/qnx_tests/cli.py" "$@"
