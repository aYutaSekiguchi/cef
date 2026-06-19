# QNX profiles extra parts client certificate factory includes

## Failure signature

Stage: compile
Category: build-graph
Target: `obj/chrome/browser/profiles/profiles_extra_parts_impl/chrome_browser_main_extra_parts_profiles.o`

Primary diagnostics:

```text
../../chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc:836:3: error: use of undeclared identifier 'client_certificates'
  client_certificates::CertificateProvisioningServiceFactory::GetInstance();
  ^
../../chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc:837:3: error: use of undeclared identifier 'client_certificates'
  client_certificates::CertificateStoreFactory::GetInstance();
  ^
```

## Root cause

QNX enables `ENTERPRISE_CLIENT_CERTIFICATES` via `enterprise_buildflags_qnx_linux_like.patch`, so `chrome_browser_main_extra_parts_profiles.cc` compiles the initialization calls for the client certificate factories.

The corresponding includes for:

- `certificate_provisioning_service_factory.h`
- `certificate_store_factory.h`

were guarded only for Android/Linux/Mac/Win. QNX therefore compiled the `ENTERPRISE_CLIENT_CERTIFICATES` use sites without seeing the `client_certificates` namespace declarations.

## Fix

Patch: `cef/patch/patches/qnx/chromium/profiles_extra_parts_client_certificates_qnx.patch`

Change:

- Add `BUILDFLAG(IS_QNX)` to the include guard in:
  - `chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc`

No BUILD change was needed because the enterprise client certificate sources/deps are already selected by `enterprise_client_certificates`.

## Verification

```text
./out/qnx_release/ninja_qnx.sh obj/chrome/browser/profiles/profiles_extra_parts_impl/chrome_browser_main_extra_parts_profiles.o
EXIT:0
```

Patch state check on the patched tree:

```text
patch -p0 --reverse --dry-run < cef/patch/patches/qnx/chromium/profiles_extra_parts_client_certificates_qnx.patch
checking file chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc
rc=0
```
