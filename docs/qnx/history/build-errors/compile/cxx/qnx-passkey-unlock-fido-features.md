# QNX: Passkey Unlock FIDO feature guards

## Stage

- stage: compile
- category: cxx / undeclared feature symbol
- target: `obj/chrome/browser/browser/passkey_unlock_manager.o`

## Failure signature

```text
../../chrome/browser/webauthn/passkey_unlock_manager.cc:121:19: error: no member named 'kPasskeyUnlockErrorUiExperimentArm' in namespace 'device'
  121 |   switch (device::kPasskeyUnlockErrorUiExperimentArm.Get()) {

../../chrome/browser/webauthn/passkey_unlock_manager.cc:162:47: error: no member named 'kPasskeyUnlockErrorUi' in namespace 'device'
  162 |   return base::FeatureList::IsEnabled(device::kPasskeyUnlockErrorUi) &&

../../chrome/browser/webauthn/passkey_unlock_manager.cc:163:39: error: no member named 'kPasskeyUnlockManager' in namespace 'device'
```

## Root cause

`passkey_unlock_manager.cc` is built for QNX and references desktop FIDO
passkey-unlock feature flags from `device/fido/public/features.h`.

Those feature declarations and definitions were guarded for:

```cpp
IS_WIN || IS_MAC || IS_LINUX || IS_CHROMEOS
```

QNX was not included, so the symbols were not declared even though the browser
code using them was compiled.

## Fix

Extend the passkey-unlock feature flag guards to include `BUILDFLAG(IS_QNX)` in:

- `device/fido/public/features.h`
- `device/fido/public/features.cc`

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/browser/passkey_unlock_manager.o \
  obj/device/fido/public/public/features.o
```

Result: `EXIT:0`; no C++ errors emitted.

## Search hints

```bash
rg -n "kPasskeyUnlockErrorUi|kPasskeyUnlockManager|PasskeyUnlockErrorUiExperimentArm" docs/qnx/history/build-errors
```
