# QNX Ozone Phase 5 — content_shell OOP Trace Run: Status Report
**Date:** 2026-07-04
**Worker:** Timed out; no active heavy processes remain.

---

## 1. Bootstrap Outcome

Bootstrap (`cef_create_projects_qnx.sh --build-type Release`) was attempted on this tree.
The bootstrap script exited non-zero and reported:

```
433 patches total (3 applied, 420 skipped, 10 failed)
!!!! ERROR: 10 patches failed to apply. Your build will not be correct.
```

**Per QNX bootstrap rules, a bootstrap with any failed patches is not clean and is not accepted.**
The script continued and generated output files in `out/qnx_release/`, but the tree state is dirty.

### Failed Patches (10)

The bootstrap log at `out/qnx_release/bootstrap_content_shell_trace.log` lists the 10 failed patches by their CEF `patch_updater.py` identifiers:

| # | Patch identifier |
|---|-----------------|
| 1 | `qnx/chromium/base_posix_elf_reader_qnx` |
| 2 | `qnx/chromium/chrome_browser_interface_binders_webui_parts_desktop_qnx` |
| 3 | `qnx/chromium/first_run_dialog_qnx` |
| 4 | `qnx/chromium/browser_window_features_profile_customization_qnx` |
| 5 | `qnx/chromium/chrome_browser_cloud_management_controller_desktop_qnx` |
| 6 | `qnx/chromium/chrome_browser_linux_is_qnx` |
| 7 | `qnx/chromium/chrome_browser_ui_version_updater_qnx` |
| 8 | `qnx/chromium/chrome_browser_enterprise_cat3_qnx` |
| 9 | `qnx/chromium/chrome_browser_ui_views_chrome_views_delegate_stub_is_qnx` |
| 10 | `qnx/chromium/chrome_browser_ui_views_status_icons_stub_is_qnx` |

Two additional patches logged as corrupt (`chrome_common_importer_BUILD_qnx.patch`, `third_party_blink_layout_stubs_qnx.patch`) but were reported as skipped — these are distinct from the 10 counted failures.

The bootstrap also produced `base/BUILD.gn.rej` (from `base_posix_elf_reader_qnx.patch` partial hunk failure).

---

## 2. Build Outcome

A `ninja` build was attempted twice on the dirty tree. It failed before producing `content_shell`. No QEMU/runtime execution occurred.

### Attempt 1 — wrong GN label

The first `ninja` invocation used `content_shell:content_shell` (wrong GN label). The error was confirmed in the log:

```
ninja: error: unknown target 'content_shell:content_shell', did you mean 'content/shell:content_shell'?
```

This attempt progressed to step `[229/68588]` before a `fieldtrial_to_struct.py` failure:

```
FAILED: gen/components/variations/field_trial_config/fieldtrial_testing_config.cc ...
fieldtrial_to_struct.py: error: option --platform: invalid choice: 'qnx' (choose from 'android', 'android_webview', 'chromeos', 'fuchsia', 'ios', 'linux', 'mac', 'windows')
```

### Attempt 2 — correct label (`content/shell:content_shell`)

The worker retried with the correct label, starting at `[1/68351]`. The log ends mid-run at approximately `[140/44295]` (subagent timeout). Build progressed only ~140 compile steps — nowhere near the full `content_shell` graph (~44,000 steps).

### Confirmed First-Actionable Compile Failures (visible in `content_shell_trace_build.log`)

| # | File | Step | Error | Severity |
|---|------|------|--------|----------|
| 1 | `components/os_crypt/sync/os_crypt_linux.cc` | ~[134/44499] | 20 errors (unknown identifiers `config_`, `DeriveV11Key`, `v11_key_`, `try_v11_`, `kDerivedKeyBytes`; `std::atomic_ref` not in `std`) — fatal error limit reached | **Blocker** |
| 2 | `components/update_client/update_query_params.cc:43` | ~[135/44499] | `#error "unknown os"` — QNX not in the switch/if chain that guards `OS_QNX` | **Blocker** |
| 3 | `gen/components/policy/policy_constants.cc:32` | ~[137/44295] | no viable conversion from `const char* const[0]` to `base::span` | **Blocker** |
| 4 | `sandbox/linux/services/proc_util.cc:105` | ~[138/44295] | `de->d_type` / `DT_LNK` — QNX `dirent` lacks `d_type` member and `DT_LNK` macro | **Blocker** |
| 5 | `sandbox/linux/services/syscall_wrappers.cc:12` | ~[139/44295] | `#include <sys/syscall.h>` — not present on QNX | **Blocker** |
| 6 | `sandbox/linux/services/scoped_process.cc:11` | ~[140/44295] | `#include <sys/syscall.h>` — not present on QNX | **Blocker** |

### Not confirmed in visible log (reported by timed-out worker; not accepted)

The timed-out worker also reported compile failures for `crashpad_linux.cc`, `crashpad.cc`, `capture_context.h`, `farmhash`, `libsync`, and `yama.cc`. These were not visible in the portion of `content_shell_trace_build.log` captured before timeout. They may appear at later compile steps (~200+) and can be confirmed only after earlier blockers are resolved.

---

## 3. Dirty State Risk

The root Chromium tree and the CEF repo both have uncommitted changes from this run:

### Root Chromium tree (`/home/yuta/chromium/src/`)
- `M build/config/qnx/qnx_std_polyfill.h` — modified by bootstrap / worker edits
- `M sandbox/policy/BUILD.gn` — worker edit from timed-out attempt
- `M third_party/cpuinfo/BUILD.gn` — worker edit
- `M third_party/farmhash/BUILD.gn` — worker edit
- `M tools/variations/fieldtrial_to_struct.py` — worker edit
- `?? base/BUILD.gn.rej` — reject file from failed `base_posix_elf_reader_qnx.patch` hunk
- ~60+ untracked new QNX platform files staged by bootstrap
- Several `.BUILD.gn.rej` reject files in `chrome/browser/`

### CEF repo (`/home/yuta/chromium/src/cef/`)
- `M BUILD.gn` — partial `is_linux && !is_qnx` crash-app disable edits from timed-out worker (unaccepted; revert or drop)
- `A?? patch/patches/qnx/chromium/crash_app_disable_linux_on_qnx.patch` — untracked patch file with partial overlap to existing `crashpad_qnx.patch` (unaccepted; drop)
- `M docs/qnx/ozone-out-of-process-gpu-plan.md` — unrelated doc edit
- `M patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc` — uncommitted diagnostic-trace source change (legitimate; commit separately)
- `M patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc` — uncommitted diagnostic-trace source change (legitimate; commit separately)

**Do not build or run QEMU from this state.** QNX bootstrap recovery is required before the next attempt.

---

## 4. `git diff --check` Result

```
(no output — no whitespace errors detected)
```
`git diff --check` ran clean; no trailing whitespace or other diff-check violations are present in the current staged/indexed changes.

---

## 5. Recommended Next Step

**QNX bootstrap recovery** via `cef_create_projects_qnx.sh` — either fix the 10 failing patches and re-bootstrap cleanly, or revert the failed patches using the command printed at bootstrap end:

```
python3 ./tools/patch_updater.py --revert \
  --patch qnx/chromium/base_posix_elf_reader_qnx \
  --patch qnx/chromium/chrome_browser_interface_binders_webui_parts_desktop_qnx \
  --patch qnx/chromium/first_run_dialog_qnx \
  --patch qnx/chromium/browser_window_features_profile_customization_qnx \
  --patch qnx/chromium/chrome_browser_cloud_management_controller_desktop_qnx \
  --patch qnx/chromium/chrome_browser_linux_is_qnx \
  --patch qnx/chromium/chrome_browser_ui_version_updater_qnx \
  --patch qnx/chromium/chrome_browser_enterprise_cat3_qnx \
  --patch qnx/chromium/chrome_browser_ui_views_chrome_views_delegate_stub_is_qnx \
  --patch qnx/chromium/chrome_browser_ui_views_status_icons_stub_is_qnx
```

Then re-bootstrap and resolve the compile blockers visible in the log (os_crypt_linux, update_query_params, policy_constants, sandbox/proc_util/syscall_wrappers/scoped_process) as a separate patch-fixing task before attempting `content/shell:content_shell` with `ninja_qnx.sh`. Crashpad/farmhash/libsync/yama failures were reported by the timed-out worker but not confirmed in the visible portion of the build log.
