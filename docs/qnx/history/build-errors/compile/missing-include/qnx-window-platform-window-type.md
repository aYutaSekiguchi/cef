# QNX window header cannot resolve PlatformWindowType

- Date: 2026-07-22
- Signature: `qnx_window.h: error: unknown type name 'PlatformWindowType'`
- Stage: compile
- Category: missing-include
- Scope: QNX Ozone `QnxWindow` transient-window implementation

## Symptoms

An intermediate context-menu fix added `PlatformWindowType` to the public
`QnxWindow` constructor and private creation helper. Every translation unit
including `qnx_window.h` then failed because that type is declared in
`platform_window_init_properties.h`, not `platform_window.h`.

## Root cause

The header exposed a type without including its declaring header. Existing
include order had hidden no equivalent dependency because `QnxWindow` had not
previously needed `PlatformWindowType`.

## Fix pattern

Include the header that owns a public type, or remove the type from the public
interface if the callee does not need it. Do not depend on transitive includes.

## Applied change

The final implementation removed the unused `PlatformWindowType` parameter
from `QnxWindow`. Parent presence alone selects `SCREEN_CHILD_WINDOW` versus
`SCREEN_APPLICATION_WINDOW`; the factory still has the complete init
properties for diagnostics and parent propagation.

## Verification

The focused `cefsimple` rebuild compiled all QNX Ozone sources, linked
`libcef.so`, and completed successfully.

## Files touched

- `patch/patches/qnx/chromium/qnx_context_menu_parent_window.patch`

## Related notes

- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-cefsimple-context-menu-unparented-window-sigstop.md`

