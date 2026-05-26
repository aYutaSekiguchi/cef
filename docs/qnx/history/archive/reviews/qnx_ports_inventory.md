# QNX Ports Inventory: Security/Crypto/RNG Ports

## Summary

**BoringSSL does not exist.** Neither do OpenSSL/LibreSSL/WolfSSL.
Only these 6 ports for security/crypto:

| Port | Purpose | Direct RNG Source |
|------|---------|-------------------|
| `gnutls` | TLS 1.2/1.3, crypto abstraction layer | ✗ (depends on nettle) |
| `nettle` | Low-level crypto/hash (AES, SHA, RSA, etc.) | ✗ |
| `s2n-tls` | TLS 1.0-1.3 (maintained by AWS) | ✗ |
| `aws-c-cal` | AWS CRT crypto abstraction | ○ (HMAC, ECDSA) |
| `aws-c-common` | AWS common foundation (aws-c-cal dependency) | △ (backend abstraction) |
| `aws-crt-cpp` | AWS C++ CRT (aws-c-cal dependency) | ✗ |

## Full Port List (164 ports)

BoringSSL/OpenSSL/LibreSSL/WolfSSL/mbedTLS **not found**.

Ports providing direct entropy/random source: **none**.

## Port Details

### gnutls/ — Main TLS Library
- Path: `/tmp/build-files/ports/gnutls/`
- Build: `make -C build-files/ports/gnutls install JLEVEL=4`
- Dependencies: gmp → nettle → gnutls
- Version: qnx-3.6.15 branch
- Note: Crypto primitives internally use nettle. RNG also via nettle

### nettle/ — Low-level Crypto Library
- Path: `/tmp/build-files/ports/nettle/`
- Build: `make -C build-files/ports/nettle/ install -j4`
- Version: nettle_3.8.1_release_20220727
- Note: Math/gmp-based crypto library providing AES/SHA/RSA. No RNG

### s2n-tls/ — AWS s2n-tls
- Path: `/tmp/build-files/ports/s2n-tls/`
- Build: `make -C build-files/ports/s2n-tls install -j4`
- Note: TLS implementation maintained by AWS. No RNG

### aws-c-cal/ — AWS CRT Crypto Abstraction
- Path: `/tmp/build-files/ports/aws-c-cal/`
- Build: `make -C build-files/ports/aws-c-cal install -j4`
- Dependencies: aws-c-common
- Note: Provides HMAC, ECDSA signing, etc. Entropy/RNG backend via aws-c-common

### aws-c-common/ — AWS Common
- Path: `/tmp/build-files/ports/aws-c-common/`
- Note: Foundation for all AWS CRT libraries. Includes platform-specific entropy source abstraction

## getentropy/getrandom Status

The following syscall/RNG-related ports do not exist in QNX Ports:
- `getentropy` (Linux-specific)
- `getrandom` (Linux-specific)
- `getauxval` / `arc4random`

QNX itself has `/dev/urandom`, so Linux-specific syscalls are not needed.

## Conclusion

1. **No BoringSSL QNX port**
2. **No OpenSSL/LibreSSL/WolfSSL/mbedTLS QNX ports either**
3. For TLS/crypto functionality, gnutls+nettle is the de facto standard
4. In AWS environment, aws-c-cal provides crypto abstraction
5. If Chromium uses Linux-specific `getrandom`/`getentropy`, a QNX polyfill implementation is needed