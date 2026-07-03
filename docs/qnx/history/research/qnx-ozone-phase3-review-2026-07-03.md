# QNX Ozone Phase 3 GN/Ozone Wiring Review

**Date:** 2026-07-03
**Scope reviewed:** `patch/patch.cfg`, the two Phase 3 QNX Chromium patches, `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/**`, `docs/qnx/history/research/qnx-ozone-phase3-gn-wiring-2026-07-03.md`, and `docs/qnx/ozone-out-of-process-gpu-plan.md`.

## Review

### Correct

- **CEF-managed locations are used.** The patch registrations are in `patch/patch.cfg:3027-3037`, the patch files live under `patch/patches/qnx/chromium/`, and the new Chromium files live under `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`.
- **Patches apply cleanly when treated as CEF/no-prefix patches.** From `/home/yuta/chromium/src`, both `patch -p0 --dry-run --batch --forward < ...` and `git apply -p0 --check ...` passed for:
  - `cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch`
  - `cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch`
- **`ozone_platform_qnx` is opt-in and default headless behavior is not changed by these patches.** The new arg is initialized to `false` in `patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch:9-10`; the auto-platform block still starts from `ozone_platform = "headless"` and `ozone_platform_headless = true` in the upstream context; the `ui/ozone/BUILD.gn` addition is gated by `if (ozone_platform_qnx)` in `patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch:9-12`.
- **Ozone generator naming expectations are mostly matched.** Running `ui/ozone/generate_constructor_list.py --platform qnx ...` produces calls to `CreateOzonePlatformQnx()` and `CreateClientNativePixmapFactoryQnx()`, which are implemented in `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:131-133` and `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.cc:11-13`.
- **No Screen/EGL link deps are introduced yet.** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:24-39` only lists Chromium deps and leaves Screen/EGL/GLES commented for later phases.

### Blocker

- **`ozone_platform_qnx=true` is likely to fail compilation because two returned `std::unique_ptr<T>` types are incomplete in `ozone_platform_qnx.cc`.**
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:12-26` includes neither `ui/ozone/public/platform_screen.h` nor `ui/base/ime/input_method.h`.
  - The same file returns `std::unique_ptr<PlatformScreen>` at `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:81-84` and `std::unique_ptr<InputMethod>` at `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:88-92`.
  - In the current upstream API, `ui/ozone/public/ozone_platform.h:39` and `ui/ozone/public/ozone_platform.h:49` only forward-declare `InputMethod` and `PlatformScreen`, while the pure virtual methods return `std::unique_ptr` at `ui/ozone/public/ozone_platform.h:296` and `ui/ozone/public/ozone_platform.h:302-304`.
  - A minimal host compile check with an incomplete `std::unique_ptr<T>` return failed with the expected libstdc++ `static_assert(sizeof(_Tp)>0)` error.
  - The target also lacks the direct dep for `ui/base/ime/input_method.h`; `ui/base/ime/BUILD.gn:60-68` owns that header, and comparable Ozone targets include `//ui/base/ime` (for example `../ui/ozone/platform/headless/BUILD.gn:35-38`).
  - **Required follow-up:** add the complete-type includes, and add `//ui/base/ime` if `input_method.h` or `input_method_minimal.h` is included from the QNX target.

### Medium

- **Phase 3 should not be marked complete yet.** The research note marks status complete and checklist items done in `docs/qnx/history/research/qnx-ozone-phase3-gn-wiring-2026-07-03.md:1-3` and `docs/qnx/history/research/qnx-ozone-phase3-gn-wiring-2026-07-03.md:157-170`, but the durable plan still has Phase 3 unchecked and requires `gn gen` / generated platform list evidence at `docs/qnx/ozone-out-of-process-gpu-plan.md:304-318`. Combined with the compile blocker above, Phase 3 needs follow-up before being accepted as complete.

### Low

- **`patch.cfg` has a misleading comment about QNX gating.** `patch/patch.cfg:3028-3029` says the flag is gated on `is_qnx`, but the patch declares `ozone_platform_qnx = false` unconditionally in `patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch:9-10`. This does not change default behavior because the value is false, but the comment should be corrected to avoid implying a guard that is not present.

## Commands run

- `tail -n 80 patch/patch.cfg && git status --short -- <review-scope>` — passed; confirmed patch.cfg entries and scoped modified/untracked files.
- `cd /home/yuta/chromium/src && patch -p0 --dry-run --batch --forward < cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch && patch -p0 --dry-run --batch --forward < cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch` — passed; both hunks applied with offsets reported by `patch`.
- `cd /home/yuta/chromium/src && git apply --check cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch && git apply --check cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch` — failed as expected for no-prefix CEF patches because default `git apply` strips one path component (`config/ozone.gni: No such file or directory`).
- `cd /home/yuta/chromium/src && git apply -p0 --check cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch && git apply -p0 --check cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch` — passed.
- `cd /home/yuta/chromium/src && python3 ui/ozone/generate_constructor_list.py --platform qnx ... && python3 ui/ozone/generate_ozone_platform_list.py --default headless headless qnx` — passed; confirmed `Qnx` constructor names and platform constants.
- `c++ -std=c++20 -c /tmp/incomplete_unique_ptr_test.cc -o /tmp/incomplete_unique_ptr_test.o` — failed with `invalid application of sizeof to incomplete type`, used to verify the incomplete-`unique_ptr` compile risk.
- Several `grep`, `find`, `read`, and `nl -ba` inspections of the reviewed files and upstream Ozone headers/BUILD files.

## Completion assessment

**Phase 3 should not be marked complete yet.** The GN patch shape is correct and opt-in, but the QNX platform target has a likely compile blocker when enabled. After fixing includes/deps, run at least `gn gen` with `ozone_platform_qnx=true` (or an equivalent bootstrap-applied GN validation) and update the durable plan checklist/evidence.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings are listed with severities and paths: blocker in patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:12-26,81-92 and BUILD.gn:24-35; medium doc completion mismatch in docs/qnx/ozone-out-of-process-gpu-plan.md:304-318 and docs/qnx/history/research/qnx-ozone-phase3-gn-wiring-2026-07-03.md:157-170; low patch.cfg comment mismatch in patch/patch.cfg:3028-3029."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-phase3-review-2026-07-03.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "cd /home/yuta/chromium/src && patch -p0 --dry-run --batch --forward < cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch && patch -p0 --dry-run --batch --forward < cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch",
      "result": "passed",
      "summary": "Both CEF/no-prefix patches dry-run applied cleanly."
    },
    {
      "command": "cd /home/yuta/chromium/src && git apply -p0 --check cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch && git apply -p0 --check cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch",
      "result": "passed",
      "summary": "Both patches passed git apply validation with -p0."
    },
    {
      "command": "cd /home/yuta/chromium/src && python3 ui/ozone/generate_constructor_list.py --platform qnx ... && python3 ui/ozone/generate_ozone_platform_list.py --default headless headless qnx",
      "result": "passed",
      "summary": "Generated names match CreateOzonePlatformQnx/CreateClientNativePixmapFactoryQnx and kPlatformQnx."
    },
    {
      "command": "c++ -std=c++20 -c /tmp/incomplete_unique_ptr_test.cc -o /tmp/incomplete_unique_ptr_test.o",
      "result": "failed",
      "summary": "Expected failure confirmed std::unique_ptr<T> return/destruction requires T to be complete."
    }
  ],
  "validationOutput": [
    "patch -p0 dry-run: checking file build/config/ozone.gni; Hunk #1 succeeded at 56 (offset 3 lines); checking file ui/ozone/BUILD.gn; Hunk #1 succeeded at 71 (offset 3 lines).",
    "git apply -p0 --check: no output, exit 0 for both patch files.",
    "generator output declared OzonePlatform* CreateOzonePlatformQnx(); and ClientNativePixmapFactory* CreateClientNativePixmapFactoryQnx(); platform list included headless then qnx."
  ],
  "residualRisks": [
    "No gn gen or ninja build was run by request; compile status remains unvalidated after the identified include/dep fix.",
    "Default headless behavior was checked by patch inspection, not by regenerating args.gn or running Chromium/CEF bootstrap."
  ],
  "noStagedFiles": true,
  "diffSummary": "Review report added only; no source files were edited by this reviewer.",
  "reviewFindings": [
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:12-26,81-92 and patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:24-35 - opt-in QNX target likely fails compile due incomplete PlatformScreen/InputMethod unique_ptr return types and missing //ui/base/ime dep.",
    "medium: docs/qnx/ozone-out-of-process-gpu-plan.md:304-318 and docs/qnx/history/research/qnx-ozone-phase3-gn-wiring-2026-07-03.md:157-170 - Phase 3 is marked complete in the research note while durable plan acceptance remains unchecked and gn gen was not run.",
    "low: patch/patch.cfg:3028-3029 - comment says the flag is gated on is_qnx, but the patch declares ozone_platform_qnx unconditionally false."
  ],
  "manualNotes": "Phase 3 needs follow-up before completion: fix includes/deps, then run bootstrap-applied gn gen with ozone_platform_qnx=true or equivalent validation."
}
```
