# QNX is_linux=false experiment — foundational commit

- Stage: compile / build-graph
- Branch: `qnx_is_linux_false_experiment`
- Date: 2026-06-11
- Commit: `e5d1c9bfc` "QNX experiment: stop treating QNX as GN Linux"
- Patches (12): see commit diff

## What this commit does

Reverts the long-standing "QNX masquerades as GN is_linux" hack in
the experiment branch. With `is_linux=false`, QNX is now an explicit
platform with its own `is_qnx=true`, and Linux assertions/feature flags
that QNX still relies on get `|| is_qnx` added in the same patch.

This is the first of a three-commit split; the subsequent commits
cover the OR-style platform asserts (commit `98d80cb09`) and the
desktop feature flags + libxml (commit `1a2a14d7d`).

## Patches in this commit

1. `build_qnx_toolchain` — drops the
   `is_linux = current_os == "linux" || is_qnx` shim so the QNX
   toolchain no longer sets `is_linux=true`.
2. `base_build_sources_qnx` — removes the now-unneeded `is_linux=false`
   workaround that excluded `linux_util.cc` from the QNX base build.
3. `protected_memory_buildflags_qnx` — same cleanup, no longer masking
   the `is_linux` branching in the protected-memory buildflags.
4. `ui_menus_allow_qnx` — `ui/menus` pulls into `ui/tests` when
   `is_linux=false`.
5. `chrome_find_bar_allow_qnx` — `chrome/test` pulls
   `chrome/browser/ui/find_bar`.
6. `ui_config_qnx_aura_views` — `use_aura` and `toolkit_views` in
   `build/config/ui.gni` get `is_qnx`.
7. `grit_args_qnx_linux_fallback` — `toolkit_views` allowlist and
   `_target_platform = "linux"` for QNX.
8. `media_cdm_paths_qnx_linux_fallback` — `component_os = "linux"`
   for QNX.
9. `chrome_browser_ui_desktop_deps_qnx` — `chrome/browser/ui:ui`
   non-Android circular include allowlist now includes `is_qnx`.
10. `enterprise_buildflags_qnx_linux_like` — enterprise features
    include `data_controls`, `watermark`, etc. for QNX.
11. `signin_features_qnx_dice_support` — DICE support
    `is_linux || is_qnx`.
12. `chrome_browser_sync_allow_qnx` — sync platform assert allows
    QNX.

## What is NOT yet done (deferred)

- OR-style platform asserts in `chrome/browser/**/BUILD.gn`,
  `chrome/renderer/**`, `chrome/common/record_replay`,
  `components/webapps/**`, `ui/base/unowned_user_data`,
  `ui/events/ozone/evdev`, `ui/ozone/platform/x11`. See commit
  `98d80cb09`.
- `enable_screen_ai_service`, `enable_speech_service`,
  `chrome_root_store_cert_management_ui`, libxml `os_include`.
  See commit `1a2a14d7d`.
- Linux-only subsystem asserts (e.g. `assert(is_linux)`, not OR-style)
  are intentionally left untouched.
- Dawn/ANGLE/breakpad `is_linux`-gated test targets that fall out of
  the graph when `is_linux=false` are still pending decision.
