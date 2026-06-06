"""``v8_unittests`` module.

Uses the per-test strategy.  V8's ``WithDefaultPlatformMixin`` instantiates
a fresh V8 platform per test fixture instance, and V8's startup state
machine is one-way -- so the binary cannot host multiple tests in the
same process.  Each test must run in a fresh ``v8_unittests`` invocation.

See ``docs/qnx/build-error-index.md`` and the structured V8-related notes
for the full history of fixes that make this strategy viable.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from ..registry import TestModule


@dataclass
class V8Module(TestModule):
    name: str = "v8"
    description: str = (
        "v8_unittests (per-test invocation, sidesteps "
        "WithDefaultPlatformMixin one-way state machine)"
    )
    binary: str = "v8_unittests"
    strategy: str = "per_test"
    default_timeout: int = 600
    default_batch_timeout: int = 7200
    # Per-test invocation: one process per test; see the structured V8 runner notes.
    one_test_per_process: bool = True
    # Honour --stack-size from the CLI; this is the calibrated default
    # that matches QNX thread stacks; see the structured V8 stack-limit note.
    per_test_args: List[str] = field(default_factory=list)
    # Parse unittests.status for [SKIP] annotations.
    parse_status_file: bool = True
    status_file_relpath: str = "v8/test/unittests/unittests.status"
