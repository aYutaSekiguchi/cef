# QNX: password credential UI controller reauth string

## Stage

- stage: compile
- category: cxx / missing-resource-id
- target: `obj/chrome/browser/browser/password_credential_ui_controller.o`

## Failure signature

```text
../../chrome/browser/webauthn/password_credential_ui_controller.cc:32:37: error: use of undeclared identifier 'IDS_PASSWORD_MANAGER_FILLING_REAUTH'
   32 |   return l10n_util::GetStringFUTF16(IDS_PASSWORD_MANAGER_FILLING_REAUTH,
      |                                     ^
1 error generated.
```

## Root cause

`IDS_PASSWORD_MANAGER_FILLING_REAUTH` is not generated for QNX. The source file
already had a Linux fallback that returns an empty message instead of using the
resource id:

```cpp
#if BUILDFLAG(IS_LINUX)
  return u"";
#else
  return l10n_util::GetStringFUTF16(IDS_PASSWORD_MANAGER_FILLING_REAUTH, ...);
#endif
```

QNX was not included in that fallback.

## Fix

Reuse the Linux fallback for QNX:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)
  return u"";
#else
  return l10n_util::GetStringFUTF16(IDS_PASSWORD_MANAGER_FILLING_REAUTH, ...);
#endif
```

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/browser/password_credential_ui_controller.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "IDS_PASSWORD_MANAGER_FILLING_REAUTH|password_credential_ui_controller|FILLING_REAUTH" docs/qnx/history/build-errors/compile
```
