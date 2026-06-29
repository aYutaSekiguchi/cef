"""``ceftests`` module.

Runs the CEF API test binary on QNX.  The target mirrors the Linux ceftests
application: it links the common CEF API/unit-test sources plus the QNX resource
locator.  QNX runs one gtest per process so a failing CEF/browser test cannot
leave global state behind and terminate the remaining suite early.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from ..registry import TestModule


@dataclass
class CefTestsModule(TestModule):
    name: str = "ceftests"
    description: str = "ceftests (CEF API validation suite, per-test invocation)"
    binary: str = "ceftests"
    strategy: str = "per_test"
    # QNX's default TMPDIR (/data/home/root/tmp) is shared by every test run
    # and can accumulate ProcessSingleton/socket leftovers after failures.
    # Use short, runner-owned local qnx6 TMPDIR paths (not NFS; AF_UNIX sockets
    # fail there) with a unique subdirectory per test invocation.
    temp_dir_relpath: str = "/data/home/root/ct"
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
