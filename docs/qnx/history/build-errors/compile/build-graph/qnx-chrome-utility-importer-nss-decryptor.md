# QNX: exclude Firefox/NSS importer from chrome utility

## Stage

- stage: compile
- category: build-graph / unsupported-platform-source
- target: `//chrome/utility:utility`
- files:
  - `chrome/utility/BUILD.gn`
  - `chrome/utility/importer/nss_decryptor.h`

## Failure signature

```text
FAILED: obj/chrome/utility/utility/nss_decryptor.o
../../chrome/utility/importer/nss_decryptor.h:16:2: error: NSSDecryptor not implemented.
   16 | #error NSSDecryptor not implemented.
      |  ^
../../chrome/utility/importer/nss_decryptor.cc:101:16: error: use of undeclared identifier 'NSSDecryptor'
```

and from Firefox importer:

```text
FAILED: obj/chrome/utility/utility/firefox_importer.o
../../chrome/utility/importer/nss_decryptor.h:16:2: error: NSSDecryptor not implemented.
../../chrome/utility/importer/firefox_importer.cc:408:3: error: unknown type name 'NSSDecryptor'
```

## Root cause

`chrome/utility/BUILD.gn` adds Firefox importer sources on all non-Android, non-ChromeOS platforms and adds `nss_decryptor.{cc,h}` on all non-Mac, non-ChromeOS platforms.

QNX entered both branches after the Chrome utility build graph became reachable. However, QNX does not enable `USE_NSS_CERTS`, and `nss_decryptor.h` only has implementations for:

- Windows: `nss_decryptor_win.h`
- `USE_NSS_CERTS`: `nss_decryptor_system_nss.h`

Otherwise it intentionally emits:

```cpp
#error NSSDecryptor not implemented.
```

## Fix

Patch: `qnx/chromium/chrome_utility_importer_exclude_nss_qnx`

Exclude QNX from the Firefox importer and NSS decryptor source branches in `chrome/utility/BUILD.gn`, matching the ChromeOS-style exclusion for unsupported importer functionality.

## Verification

After `gn gen`, the stale object targets were no longer present:

```text
ninja: error: unknown target 'obj/chrome/utility/utility/firefox_importer.o'
```

A wider `ninja -C . chrome/utility:utility` progressed past importer/NSS and exposed the next blocker in `components/autofill/core/browser/payments/full_card_request.cc`.

## Search hints

```bash
rg -n "NSSDecryptor not implemented|firefox_importer|nss_decryptor|USE_NSS_CERTS" /tmp/*.log docs/qnx/history/build-errors
```
