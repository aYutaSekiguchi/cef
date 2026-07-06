# QNX crashpad client stub symbols - duplicate removal

## Summary

A redundant set of no-op `crashpad::` client symbols was added to the
`//third_party/crashpad/crashpad/handler:handler` target by
`crashpad_handler_qnx_noop_client_stubs.patch`. These symbols already exist
in `components/crash/core/app/crashpad_qnx.cc` (added by `crashpad_qnx.patch`
and documented in `qnx-crashpad-qnx-stubs.md`).

When `content_shell` (and any other binary that links both
`components/crash/core/app:app` and `components/crash/core/app:chrome_crashpad_handler`)
is built, both sets of definitions get linked into the same binary and the
linker reports multiple-definition errors for:

```text
crashpad::CrashpadClient::CrashpadClient()
crashpad::CrashpadClient::~CrashpadClient()
crashpad::CrashpadClient::StartHandler(...)
crashpad::CrashReportDatabase::Initialize(...)
crashpad::CrashReportDatabase::InitializeWithoutCreating(...)
crashpad::HTTPTransport::Create()
crashpad::Paths::Executable(...)
```

## Root cause

The `:handler` target on QNX only included the two no-op stub sources
(`handler_main.cc`, `prune_crash_reports_thread.cc`) plus the redundant
`qnx/noop_client_stubs.cc` added by the redundant patch. None of these
sources (including the CEF-specific upload thread / utils when
`enable_cef`) actually reference `crashpad::CrashpadClient`,
`crashpad::CrashReportDatabase`, `crashpad::HTTPTransport`, or
`crashpad::Paths` -- those are client-side APIs used by the browser
process, not by the crashpad handler process.

## Fix

Delete the redundant patch and file:

- Remove `cef/patch/patches/qnx/chromium/crashpad_handler_qnx_noop_client_stubs.patch`
- Remove `cef/patch/qnx/chromium/new_files/third_party/crashpad/crashpad/handler/qnx/noop_client_stubs.cc`
- Remove the corresponding entry from `cef/patch/patch.cfg`

The canonical stubs in `components/crash/core/app/crashpad_qnx.cc` remain
and satisfy all link references.

## Verification

- `git apply -p0 --check` on the trimmed patch stack succeeds.
- `cef_create_projects_qnx.sh` reports `0 failed` patches.
- `ninja -C out/qnx_release content_shell:content_shell` no longer fails with
  `multiple definition of 'crashpad::CrashpadClient::...'` etc.

## Search hints

```bash
rg -n 'noop_client_stubs|crashpad_handler_qnx_noop' cef/patch
```