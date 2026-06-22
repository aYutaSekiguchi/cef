# QNX crashpad client stub symbols

## Summary

QNX intentionally does not build the full crashpad client library
(`crashpad_buildconfig_disable_linux_qnx.patch` clears `crashpad_is_linux` for
QNX). The QNX integration file `components/crash/core/app/crashpad_qnx.cc`
provides no-op crash reporter helpers, but Chromium/CEF still references a few
`crashpad::` class symbols directly, leaving them unresolved in `libcef.so`:

```text
crashpad::CrashpadClient::CrashpadClient()
crashpad::CrashpadClient::~CrashpadClient()
crashpad::CrashpadClient::StartHandler(...)
crashpad::CrashReportDatabase::Initialize(...)
crashpad::CrashReportDatabase::InitializeWithoutCreating(...)
crashpad::HTTPTransport::Create()
crashpad::Paths::Executable(...)
```

## Fix

New file:

```text
cef/patch/qnx/chromium/new_files/components/crash/core/app/crashpad_qnx.cc
```

Add minimal no-op definitions for the missing `crashpad::` client symbols in
the QNX integration file. These stubs are enough for the desktop code paths to
link while crash reporting remains disabled at runtime
(`InitializeCrashpad` returns `false`).

## Verification

```bash
source out/qnx_release/qnx_env.sh
autoninja -C out/qnx_release cefsimple
```

`grep 'undefined reference' /tmp/cefsimple_build.log | grep 'crashpad::'`
returned no lines. Distinct undefined references dropped from 98 to 91.

## Search hints

```bash
rg -n "crashpad::CrashpadClient|crashpad::CrashReportDatabase|crashpad::HTTPTransport|crashpad::Paths|crashpad_qnx" docs/qnx/history/build-errors
```
