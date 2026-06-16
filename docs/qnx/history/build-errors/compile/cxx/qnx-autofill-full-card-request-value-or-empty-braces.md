# QNX: autofill full_card_request optional value_or empty braces

## Stage

- stage: compile
- category: cxx / libc++ compatibility
- target: `//components/autofill/core/browser:browser`
- file: `components/autofill/core/browser/payments/full_card_request.cc`

## Failure signature

```text
../../components/autofill/core/browser/payments/full_card_request.cc:127:54: error: no matching member function for call to 'value_or'
  request_->context_token = std::move(context_token).value_or({});
                            ~~~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~
/home/yuta/qnx800/target/qnx/usr/include/c++/v1/optional:850:46: note: candidate template ignored: couldn't infer template argument '_Up'
  constexpr value_type value_or(_Up&& __v) const&
```

## Root cause

QNX libc++ does not infer the template argument for `std::optional<std::string>::value_or({})`. The same pattern was already handled in `qnx/chromium/profile_network_context_service_qnx` for `std::optional<std::vector<uint8_t>>::value_or({})` by replacing the braced fallback with an explicit value type.

## Fix

Patch: `qnx/chromium/autofill_full_card_request_value_or_qnx`

Use an explicit empty string fallback:

```cpp
request_->context_token = std::move(context_token).value_or(std::string{});
```

## Verification

Narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/components/autofill/core/browser/browser/full_card_request.o
```

Result:

```text
[100/100] CXX obj/components/autofill/core/browser/browser/full_card_request.o
```

## Search hints

```bash
rg -n "value_or\(\{\}\)|couldn't infer template argument '_Up'|full_card_request" /tmp/*.log docs/qnx/history/build-errors
```
