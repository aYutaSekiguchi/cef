# QNX: cloud management controller desktop guards

## Stage

- stage: compile
- category: cxx / platform guard
- target: `obj/chrome/browser/browser/chrome_browser_cloud_management_controller_desktop.o`

## Failure signature

```text
../../chrome/browser/policy/chrome_browser_cloud_management_controller_desktop.cc:114:19: error: use of undeclared identifier 'BrowserDMTokenStorage'
  114 |   std::unique_ptr<BrowserDMTokenStorage::Delegate> storage_delegate;

../../chrome/browser/policy/chrome_browser_cloud_management_controller_desktop.cc:275:32: error: no member named 'SaasUsageReportingDelegateFactoryDesktop' in namespace 'enterprise_reporting'
  275 |   return enterprise_reporting::SaasUsageReportingDelegateFactoryDesktop::
```

## Root cause

`chrome_browser_cloud_management_controller_desktop.cc` is built for QNX via
the non-Android/non-ChromeOS desktop browser source set. However, helper code in
that file assumed only Win/Mac/Linux desktop builds:

- `BrowserDMTokenStorageLinux` was included and used only for
  `IS_LINUX || IS_CHROMEOS`, excluding QNX.
- `BrowserDMTokenStorage` was only made visible through those platform storage
  headers, so QNX had no declaration at the use site.
- `SaasUsageReportingDelegateFactoryDesktop` is only included under
  `IS_WIN || IS_LINUX || IS_MAC`, but `GetSaasUsageReportingDelegateFactory()`
  returned it for every non-ChromeOS platform, including QNX.
- The Linux DM token storage implementation was not part of the QNX browser
  sources.

## Fix

- Reuse `BrowserDMTokenStorageLinux` for QNX by extending the include and
  delegate-selection guard to `IS_QNX`.
- Add `policy/browser_dm_token_storage_linux.{cc,h}` to the QNX browser source
  set.
- Restrict `SaasUsageReportingDelegateFactoryDesktop::CreateForBrowser()` to
  Win/Mac/Linux and return `nullptr` elsewhere, including QNX. This avoids
  pulling the large desktop SaaS usage reporting source block into QNX.

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/browser/chrome_browser_cloud_management_controller_desktop.o

QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/browser/browser_dm_token_storage_linux.o
```

Both commands returned `EXIT:0`.

## Search hints

```bash
rg -n "BrowserDMTokenStorage|SaasUsageReportingDelegateFactoryDesktop|chrome_browser_cloud_management_controller_desktop" docs/qnx/history/build-errors
```
