"""``ceftests`` module.

Runs the CEF API test binary on QNX.  The target mirrors the Linux ceftests
application: it links the common CEF API/unit-test sources plus the QNX resource
locator and uses the standard broad-run gtest strategy.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from ..registry import TestModule


@dataclass
class CefTestsModule(TestModule):
    name: str = "ceftests"
    description: str = "ceftests (CEF API validation suite)"
    binary: str = "ceftests"
    strategy: str = "single"
    default_timeout: int = 600
    default_batch_timeout: int = 7200
    default_exclusions: List[str] = field(default_factory=list)
    per_test_args: List[str] = field(
        default_factory=lambda: [
            "--ozone-platform=headless",
            "--disable-gpu",
            "--disable-gpu-compositing",
        ]
    )
