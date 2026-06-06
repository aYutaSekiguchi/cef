# fieldtrial_to_struct.py must accept --platform=qnx

- Date: 2026-06-03
- Signature: fieldtrial_to_struct.py: error: option --platform: invalid choice: 'qnx'
- Stage: compile
- Category: feature-guard
- Scope: variations fieldtrial generator

## Symptoms

- The build failed while generating `fieldtrial_testing_config.cc`.
- `fieldtrial_to_struct.py` rejected `--platform=qnx` as an invalid choice.

## Root cause

- The script hard-coded the legal `--platform` values and omitted `qnx`.
- The QNX GN toolchain legitimately passed `--platform=qnx`, matching `target_os = "qnx"`.
- The underlying variations pipeline already had a valid `Study::PLATFORM_QNX` enum path, so the restriction was only in the script's option list.

## Fix pattern

- When a generator script validates platform names with a local allowlist, update the allowlist before looking for deeper build-system workarounds.
- Prefer the smallest change at the validation boundary when the downstream schema already supports the platform.

## Applied change

- Added `qnx` to the `_platforms` list in `tools/variations/fieldtrial_to_struct.py`.
- Captured the edit as `cef/patch/patches/qnx/chromium/fieldtrial_to_struct_qnx.patch`.
- Registered the patch in `cef/tools/cef_create_projects_qnx.sh`.

## Verification

- The fieldtrial config action ran to completion.
- `fieldtrial_testing_config.cc` was generated successfully.

## Files touched

- `tools/variations/fieldtrial_to_struct.py`
- `cef/patch/patches/qnx/chromium/fieldtrial_to_struct_qnx.patch`
- `cef/tools/cef_create_projects_qnx.sh`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/gn/build-graph/ffmpeg-qnx-platform-config-directory-missing.md`
