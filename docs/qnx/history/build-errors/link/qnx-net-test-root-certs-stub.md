# QNX net::TestRootCerts platform stub

## Summary

`net::TestRootCerts` has three platform hooks declared in
`net/cert/test_root_certs.h` and defined in
`net/cert/test_root_certs_*.cc`:

- `void Init() EXCLUSIVE_LOCKS_REQUIRED(lock_)`
- `bool AddImpl(X509Certificate*) EXCLUSIVE_LOCKS_REQUIRED(lock_)`
- `void ClearImpl() EXCLUSIVE_LOCKS_REQUIRED(lock_)`

The implementations live in:

- `test_root_certs_builtin.cc` (apple / win / use_nss_certs / fuchsia)
- `test_root_certs_android.cc`
- `test_root_certs_ios.cc`

QNX is none of those (QNX is not apple, not win, not android, not ios;
QNX has `use_nss_certs=false` in `args.gn`, and is not fuchsia), so
`libcef.so` fails to link with:

```text
undefined reference to `net::TestRootCerts::ClearImpl()'
undefined reference to `net::TestRootCerts::Init()'
undefined reference to `net::TestRootCerts::AddImpl(X509Certificate*)'
```

## Patches

```text
cef/patch/patches/qnx/chromium/net_test_root_certs_qnx.patch
```

### New files

```text
cef/patch/qnx/chromium/new_files/net/cert/test_root_certs_qnx.cc
```

## Details

- Add `test_root_certs_qnx.cc` with no-op `Init()`, `AddImpl()`, and
  `ClearImpl()` (same shape as the builtin fallback).
- Add `if (is_qnx) { sources += [ "cert/test_root_certs_qnx.cc" ] }` to
  `net/BUILD.gn` so the stub is compiled for QNX.

## Verification

```bash
autoninja -C out/qnx_release cefsimple
```

Removes the three `net::TestRootCerts::*` undefined references; other
net/ platform categories (e.g. `net::PlatformMimeUtil::*`) are handled
separately.

## Search hints

```bash
rg -n "TestRootCerts::ClearImpl|TestRootCerts::Init|TestRootCerts::AddImpl|net_test_root_certs_qnx" docs/qnx/history/build-errors
```
