"""``base_unittests`` module -- the historical default broad-run target.

Default exclusions follow the current ``QNX_ENV_EXCLUSIONS`` list baked
into ``qnx_run_test.sh``.  See ``docs/qnx/build-error-index.md``
sections 6, 8, 19 and 20 for the rationale behind each pattern.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from ..registry import TestModule


@dataclass
class BaseModule(TestModule):
    name: str = "base"
    description: str = "base_unittests (broad run, default target)"
    binary: str = "base_unittests"
    strategy: str = "single"
    default_timeout: int = 600
    default_batch_timeout: int = 7200
    # The three accepted exclusions tracked in
    # docs/qnx/build-error-index.md ("Current accepted exclusions").
    default_exclusions: List[str] = field(default_factory=lambda: [
        "StackTraceDeathTest.StackDumpSignalHandlerIsMallocFree",
        "ImportantFileWriterTest.FailedWriteWithObserver",
        "*AnyCriticalThreadHung*",
    ])
