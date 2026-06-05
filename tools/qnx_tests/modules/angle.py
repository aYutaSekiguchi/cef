"""``angle`` test group.

Runs the three ANGLE test binaries sequentially using the ``single``
strategy (one broad invocation per binary):

* ``angle_system_info_test``
* ``angle_unittests``
* ``angle_end2end_tests``

These are the current QNX bring-up targets for ANGLE.  Keeping them in a
single module makes ``cef/tools/qnx_run_test.sh --angle`` the obvious
smoke/integration entry point after building the corresponding targets.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from ..registry import BinarySpec, TestModule


@dataclass
class AngleModule(TestModule):
    name: str = "angle"
    description: str = (
        "ANGLE test group "
        "(angle_system_info_test + angle_unittests + angle_end2end_tests)"
    )
    strategy: str = "single"
    default_timeout: int = 600
    default_batch_timeout: int = 7200
    per_test_args: List[str] = field(default_factory=list)

    binaries: List[BinarySpec] = field(default_factory=lambda: [
        BinarySpec(
            name="angle_system_info_test",
            description="angle_system_info_test (broad run)",
            strategy="single",
        ),
        BinarySpec(
            name="angle_unittests",
            description="angle_unittests (broad run)",
            strategy="single",
        ),
        BinarySpec(
            name="angle_end2end_tests",
            description="angle_end2end_tests (broad run)",
            strategy="single",
            default_batch_timeout=10800,
        ),
    ])
