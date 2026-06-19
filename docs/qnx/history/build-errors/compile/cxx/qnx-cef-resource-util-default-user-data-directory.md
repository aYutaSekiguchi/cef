# QNX CEF resource_util default user data directory

## Failure signature

Stage: compile
Category: cxx
Target: `obj/cef/libcef_static/resource_util.o`

Primary diagnostic:

```text
../../cef/libcef/common/resource_util.cc:96:7: error: use of undeclared identifier 'GetDefaultUserDataDirectory'
  if (GetDefaultUserDataDirectory(&result)) {
      ^~~~~~~~~~~~~~~~~~~~~~~~~~~
```

## Root cause

`cef/libcef/common/resource_util.cc` defines its local `GetDefaultUserDataDirectory()` helper only for Linux, Mac, and Windows. QNX is POSIX-like and uses the same default `~/.config/cef_user_data` behavior as Linux, but it was excluded from the Linux guard.

## Fix

Direct CEF source change:

- Add `BUILDFLAG(IS_QNX)` to the Linux guards in `cef/libcef/common/resource_util.cc` so QNX uses the Linux/XDG-style `cef_user_data` default path.

## Verification

```text
./out/qnx_release/ninja_qnx.sh obj/cef/libcef_static/resource_util.o
EXIT:0
```

## Search hints

```bash
rg -n "GetDefaultUserDataDirectory|resource_util.cc|cef_user_data|XDGDirectory" docs/qnx/history/build-errors
```
