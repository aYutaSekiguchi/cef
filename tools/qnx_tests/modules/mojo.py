"""``mojo_unittests`` module.

Runs the Mojo unit test binary built from the ``mojo:mojo_unittests`` target
in Chromium.  This is the canonical "build the whole mojo unit surface and
run it on QNX" check used to verify the QNX port of the Mojo IPC core,
including the named_mojo_ipc_server QNX endpoint connector backend.

See ``docs/qnx/build-error-index.md`` and the structured notes under
``docs/qnx/history/build-errors/`` for the rationale and the patches that
make this build cleanly on QNX.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from ..registry import TestModule


@dataclass
class MojoModule(TestModule):
    name: str = "mojo"
    description: str = (
        "mojo_unittests (qemu guest runs the mojo:mojo_unittests binary; "
        "exercises the QNX port of the Mojo IPC core, including the QNX "
        "named_mojo_ipc_server endpoint connector backend)"
    )
    binary: str = "mojo_unittests"
    strategy: str = "single"
    default_timeout: int = 600
    default_batch_timeout: int = 7200
    # Mojo uses TestSuiteBase and its death-test config rather than a
    # one-way V8-style state machine, so a single broad run is acceptable.
    # Exclusions are populated as the QNX port progresses; for now leave
    # the list empty and let real failures surface so the patches can be
    # tightened iteratively.
    default_exclusions: List[str] = field(default_factory=list)
