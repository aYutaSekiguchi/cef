"""Test module registry.

Adding a new module:

1.  Drop a new file under this directory (e.g. ``cctest.py``) defining
    a ``TestModule`` subclass named ``<Name>Module``.
2.  Import it below and add it to ``MODULES``.

The CLI flag is ``--<module.name>`` (kebab-case preserved); the
registered entry in ``MODULES`` is keyed by ``module.name``.
"""

from __future__ import annotations

from qnx_tests.modules.angle import AngleModule
from qnx_tests.modules.base import BaseModule
from qnx_tests.modules.v8 import V8Module
from qnx_tests.modules.swiftshader import SwiftShaderModule


MODULES = {
    "base": BaseModule(),
    "v8": V8Module(),
    "swiftshader": SwiftShaderModule(),
    "angle": AngleModule(),
}


__all__ = [
    "MODULES",
    "AngleModule",
    "BaseModule",
    "V8Module",
    "SwiftShaderModule",
]
