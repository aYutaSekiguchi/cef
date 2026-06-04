#!/usr/bin/env python3
"""Backwards-compatibility shim for the old v8_unittests runner.

The full implementation has been folded into the unified test runner
under ``tools/qnx_tests/``.  This file remains so that existing
documentation, scripts and muscle memory continue to work.

The new canonical command is::

    ./tools/qnx_run_test.sh --v8 [args...]

All flags supported by the old script (``--filter``, ``--skip-death-tests``,
``--timeout``, ``--boot-timeout``, ``--kill-existing``, ``--dry-run``,
``--max-tests``, ``--stack-size``) are forwarded unchanged.  See
``tools/qnx_tests/cli.py --help`` for the canonical option list.
"""

from __future__ import annotations

import os
import sys


_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from qnx_tests.cli import main  # noqa: E402


def _emit_warning() -> None:
    sys.stderr.write(
        "NOTE: qnx_run_v8_unittests.py is a backwards-compatibility shim.\n"
        "      The new canonical command is:\n"
        "          ./tools/qnx_run_test.sh --v8 [args...]\n"
        "      This shim will be removed in a future CEF upgrade.\n\n"
    )


if __name__ == "__main__":
    _emit_warning()
    sys.argv.insert(1, "--v8")
    sys.exit(main())
