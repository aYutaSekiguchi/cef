"""``swiftshader`` test group.

Runs the three swiftshader gtest binaries sequentially using the
``single`` strategy (one broad invocation per binary):

* ``swiftshader_system_unittests``
* ``swiftshader_reactor_llvm_unittests``
* ``swiftshader_reactor_subzero_unittests``

The ``single`` strategy avoids ``--gtest_list_tests`` because that
helper crashes the QNX guest with an ``Invalid file descriptor to
ICU data`` error from ``base/i18n/icu_util.cc:232`` (the Chromium
test-launcher initialises ICU before gtest enumeration, and the
file-descriptor handoff does not survive the QEMU serial console
round-trip).  Operators that need stack-sensitive per-test
invocation should run the binary directly on the guest with
``--gtest_filter=<one>``.
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
        "(system_unittests + reactor_llvm_unittests + reactor_subzero_unittests, "
        "all broad-run)"
    )
    # All three binaries use the 'single' strategy to avoid the
    # --gtest_list_tests + ICU file-descriptor handoff issue in QEMU.
    strategy: str = "single"
    default_timeout: int = 600
    default_batch_timeout: int = 7200
    per_test_args: List[str] = field(default_factory=list)

    binaries: List[BinarySpec] = field(default_factory=lambda: [
        BinarySpec(
            name="swiftshader_system_unittests",
            description="swiftshader_system_unittests (broad run)",
            strategy="single",
        ),
        BinarySpec(
            name="swiftshader_reactor_llvm_unittests",
            description="swiftshader_reactor_llvm_unittests (broad run)",
            strategy="single",
        ),
        BinarySpec(
            name="swiftshader_reactor_subzero_unittests",
            description="swiftshader_reactor_subzero_unittests (broad run)",
            strategy="single",
        ),
    ])
