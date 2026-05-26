# BoringSSL `getentropy` Analysis for QNX

## Files Retrieved
1. `third_party/boringssl/src/crypto/rand/getentropy.cc` (lines 1-50) - main getentropy implementation
2. `third_party/boringssl/src/crypto/rand/internal.h` (lines 1-50) - platform detection macros
3. `third_party/boringssl/src/crypto/rand/urandom.cc` (lines 1-125) - Linux alternative using `/dev/urandom` and `getrandom`

## Key Code

### Platform Detection (`internal.h` lines 18-35)
```c
#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION)
#define OPENSSL_RAND_DETERMINISTIC
#elif defined(OPENSSL_TRUSTY)
#define OPENSSL_RAND_TRUSTY
#elif defined(OPENSSL_WINDOWS)
#define OPENSSL_RAND_WINDOWS
#elif defined(OPENSSL_LINUX)
#define OPENSSL_RAND_URANDOM
#elif defined(OPENSSL_APPLE) && !defined(OPENSSL_MACOS)
#define OPENSSL_RAND_IOS
#else
// By default if you are integrating BoringSSL we expect you to
// provide getentropy from the <unistd.h> header file.
#define OPENSSL_RAND_GETENTROPY
#endif
```

### getentropy Implementation (`getentropy.cc`)
```c
void bssl::CRYPTO_sysrand(uint8_t *out, size_t requested) {
  while (requested > 0) {
    size_t todo = requested <= 256 ? requested : 256;  // getentropy max 256 bytes
    if (getentropy(out, todo) != 0) {
      perror("getentropy() failed");
      abort();
    }
    out += todo;
    requested -= todo;
  }
}
```

## Architecture

### RAND Module Flow
1. `RAND_bytes()` → `BCM_rand_bytes()` → `CRYPTO_sysrand()`
2. Platform selection happens at compile-time via macros
3. Four platform implementations:
   - `OPENSSL_RAND_GETENTROPY` → `getentropy.cc` (default fallback)
   - `OPENSSL_RAND_URANDOM` → `urandom.cc` (Linux: `/dev/urandom` + `getrandom`)
   - `OPENSSL_RAND_WINDOWS` → `windows.cc` (BCrypt/ProcessPrng)
   - `OPENSSL_RAND_IOS` → `ios.cc` (iOS SecRandomCopyBytes)
   - `OPENSSL_RAND_TRUSTY` → `trusty.cc` (Trusty TEE)

### urandom.cc Fallback Chain (Linux)
```c
1. Try `getrandom()` syscall (via `syscall(__NR_getrandom, ...)`)
2. If ENOSYS → fall back to `/dev/urandom` via `read()`
3. FIPS builds abort if getrandom not available
```

## QNX Situation

### Current State
- QNX not recognized in BoringSSL platform detection
- Falls through to `OPENSSL_RAND_GETENTROPY` (default)
- Requires system `getentropy()` in `<unistd.h>`

### QNX Availability
| Feature | Status |
|---------|--------|
| `getentropy()` | QNX Neutrino 6.5+ has it (POSIX compliance) |
| `/dev/urandom` | QNX provides `/dev/random` but not `/dev/urandom` |
| `getrandom()` | Not available on QNX |

### Alternative Implementations for QNX
1. **Use getentropy** (if QNX 6.5+): Simplest - set `OPENSSL_RAND_GETENTROPY`
2. **Use `/dev/random` via read()**: Create urandom.cc variant for QNX
3. **Use QNX `DevurandGenerate()`**: QNX-specific API if available
4. **Define custom macro**: e.g., `#define OPENSSL_QNX` with custom impl

## Recommendations

### Option A: Use System getentropy (Simplest)
If QNX 6.5+ is used, no changes needed. QNX provides `getentropy()`.

### Option B: Custom Platform Branch
Modify `internal.h`:
```c
#elif defined(OPENSSL_QNX)
#define OPENSSL_RAND_QNX
```

### Option C: Reuse urandom.cc with Path Adaptation
Modify urandom.cc to handle `/dev/random` instead of `/dev/urandom`:
```c
fd = open("/dev/random", O_RDONLY | O_CLOEXEC);
```

## Start Here
File: `third_party/boringssl/src/crypto/rand/internal.h`  
Why: Add QNX detection macro (line ~28) and QNX-specific fork detection (line ~49)