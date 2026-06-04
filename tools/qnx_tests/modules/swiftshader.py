"""``swiftshader`` test group.

Runs the three swiftshader gtest binaries sequentially:

* ``swiftshader_system_unittests``        - broad run
* ``swiftshader_reactor_llvm_unittests``  - per-test (stack-sensitive)
* ``swiftshader_reactor_subzero_unittests`` - per-test (stack-sensitive)

Reactor LLVM and Subzero unit tests are stateful and stack-sensitive on
QEMU, and several tests exceed the QNX default thread stack unless the
per-test ``--stack-size`` is honoured (see sections 31, 32 and 41).
System unittests, on the other hand, are stable enough to run as a
single broad invocation.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List

from ..registry import BinarySpec, TestModule


@dataclass
class SwiftShaderModule(TestModule):
    name: str = "swiftshader"
    description: str = (
        "swiftshader test group "
        "(system_unittests + reactor_llvm_unittests + reactor_subzero_unittests)"
    )
    # Per-binary strategy defaults inherited by every BinarySpec.
    strategy: str = "single"
    default_timeout: int = 600
    default_batch_timeout: int = 7200
    # No global per_test_args; the operator can still pass
    # --stack-size on the CLI which is forwarded to each test.
    per_test_args: List[str] = field(default_factory=list)
    # Stack-sensitive Reactor binaries must run one test per process.
    one_test_per_process: bool = True

    binaries: List[BinarySpec] = field(default_factory=lambda: [
        BinarySpec(
            name="swiftshader_system_unittests",
            description="swiftshader_system_unittests (broad run)",
            strategy="single",
        ),
        BinarySpec(
            name="swiftshader_reactor_llvm_unittests",
            description=(
                "swiftshader_reactor_llvm_unittests "
                "(per-test, stack-sensitive)"
            ),
            strategy="per_test",
            one_test_per_process=True,
        ),
        BinarySpec(
            name="swiftshader_reactor_subzero_unittests",
            description=(
                "swiftshader_reactor_subzero_unittests "
                "(per-test, stack-sensitive)"
            ),
            strategy="per_test",
            one_test_per_process=True,
        ),
    ])
