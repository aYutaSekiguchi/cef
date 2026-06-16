# QNX: autofill flat_hash_set std::from_range constructor

## Stage

- stage: compile
- category: cxx / libc++ compatibility
- target: `//components/autofill/core/browser:browser`
- files:
  - `components/autofill/core/browser/webdata/addresses/autofill_profile_sync_bridge.cc`
  - `components/autofill/core/browser/webdata/autocomplete/autocomplete_sync_bridge.cc`
  - `components/autofill/core/browser/webdata/payments/autofill_wallet_credential_sync_bridge.cc`
  - `components/autofill/core/browser/webdata/payments/autofill_wallet_metadata_sync_bridge.cc`

## Failure signature

```text
error: no matching constructor for initialization of 'absl::flat_hash_set<std::string>'
  absl::flat_hash_set<std::string> keys_set(std::from_range, storage_keys);
                                   ^        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
../../third_party/abseil-cpp/absl/container/internal/raw_hash_set.h:2140:3: note: candidate template ignored: deduced conflicting types for parameter 'InputIter' ('from_range_t' vs. 'StorageKeyList')
```

The same pattern appeared first in four autofill sync bridges and then in two valuables sync bridges.

## Root cause

The QNX build uses the QNX libc++/Abseil combination where `absl::flat_hash_set` does not provide or select a `std::from_range` constructor. The portable constructor from iterators is available.

## Fix

Patches:

- `qnx/chromium/autofill_flat_hash_set_from_range_qnx`
- `qnx/chromium/autofill_valuables_flat_hash_set_from_range_qnx`

Replace:

```cpp
absl::flat_hash_set<std::string> keys_set(std::from_range, storage_keys);
```

with:

```cpp
absl::flat_hash_set<std::string> keys_set(storage_keys.begin(),
                                         storage_keys.end());
```

and apply the same transformation for `storage_keys_set` in payment metadata/credential sync bridges and valuables sync bridges.

## Verification

Narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . \
  obj/components/autofill/core/browser/browser/autofill_profile_sync_bridge.o \
  obj/components/autofill/core/browser/browser/autocomplete_sync_bridge.o \
  obj/components/autofill/core/browser/browser/autofill_wallet_credential_sync_bridge.o \
  obj/components/autofill/core/browser/browser/autofill_wallet_metadata_sync_bridge.o \
  obj/components/autofill/core/browser/browser/valuable_sync_bridge.o \
  obj/components/autofill/core/browser/browser/valuable_metadata_sync_bridge.o
```

Result:

```text
[100/103] CXX obj/components/autofill/core/browser/browser/autofill_wallet_credential_sync_bridge.o
[101/103] CXX obj/components/autofill/core/browser/browser/autofill_profile_sync_bridge.o
[102/103] CXX obj/components/autofill/core/browser/browser/autocomplete_sync_bridge.o
[103/103] CXX obj/components/autofill/core/browser/browser/autofill_wallet_metadata_sync_bridge.o
[100/101] CXX obj/components/autofill/core/browser/browser/valuable_sync_bridge.o
[101/101] CXX obj/components/autofill/core/browser/browser/valuable_metadata_sync_bridge.o
```

## Search hints

```bash
rg -n "std::from_range|flat_hash_set<std::string>|deduced conflicting types for parameter 'InputIter'" /tmp/*.log docs/qnx/history/build-errors
```
