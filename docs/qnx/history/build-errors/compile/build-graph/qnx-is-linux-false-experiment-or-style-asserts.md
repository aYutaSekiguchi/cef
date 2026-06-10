# QNX is_linux=false experiment — OR-style platform asserts

- Stage: compile / build-graph
- Branch: `qnx_is_linux_false_experiment`
- Date: 2026-06-11
- Commit: `98d80cb09` "QNX experiment: allow is_qnx in OR-style platform asserts"
- Patches (6): see commit diff

## What this commit does

With `is_linux=false`, the chrome/browser/** and chrome/renderer
BUILD.gn files that used to be reachable through `is_linux=true` now
fail GN evaluation unless they get `is_qnx` added explicitly. This
commit groups all OR-style platform assertions (the ones that list
`is_linux` alongside `is_win`/`is_mac`/`is_chromeos`/`is_android`)
and adds `|| is_qnx` to each.

## Patches in this commit

1. `chrome_views_toolbar_allow_qnx` — single file:
   `chrome/browser/ui/views/toolbar/BUILD.gn:5`.
2. `chrome_browser_ui_asserts_allow_qnx` — bulk patch over 68 files
   in `chrome/browser/ui/**`, excluding
   `chrome/browser/ui/BUILD.gn` (handled by `enable_on_device_model`),
   `chrome/browser/ui/find_bar`, and `chrome/browser/ui/views/toolbar`.
3. `chrome_browser_asserts_allow_qnx` — bulk patch over
   `chrome/browser/**/BUILD.gn` excluding all `ui` subpaths and
   `chrome/browser/sync`. Multi-line aware: a `,` inside the assert
   block is preferred over a `)`-terminated line.
4. `ui_asserts_allow_qnx` — three files:
   `ui/base/unowned_user_data/BUILD.gn`,
   `ui/events/ozone/evdev/BUILD.gn`,
   `ui/ozone/platform/x11/BUILD.gn`.
5. `components_asserts_allow_qnx` — two multi-line asserts in
   `components/webapps/isolated_web_apps/BUILD.gn` and
   `components/webapps/isolated_web_apps/test_support/BUILD.gn`.
6. `chrome_nonbrowser_asserts_allow_qnx` — `chrome/common/record_replay`,
   `chrome/renderer/record_replay`, four `chrome/test/data/webui/**`.

## Multi-line assert handling

Asserts that span multiple lines — e.g.
```gn
assert(is_chromeos || is_win || is_mac || is_linux,
       "components/webapps/isolated_web_apps are no-op on Android or iOS.")
```
were processed by scanning a balanced parenthesis block and inserting
`|| is_qnx` before the first `,` (or before the closing `)` if there
is no comma). The earlier single-line-only script missed these and
the experiment branch bootstrap stalled at the multi-line
`components/webapps/isolated_web_apps` asserts; the regenerated
patch unblocks both the chrome and components passes.

## What is NOT yet done (deferred)

- Linux-only subsystem asserts (`assert(is_linux)`, not OR-style)
  are intentionally left untouched; they will be handled in a later
  commit if/when the targets in question are needed for
  `base_unittests` on QNX.
- Dawn/ANGLE/breakpad `is_linux`-gated test targets that fall out
  of the graph when `is_linux=false` are still pending decision.
