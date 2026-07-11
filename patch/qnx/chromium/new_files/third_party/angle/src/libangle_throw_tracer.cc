// libangle_throw_tracer.cc
// QNX ANGLE EGL abort diagnostic tracer — production wrapper.
//
// Build: -fexceptions -fno-rtti -fvisibility=default -fPIC -shared
//   q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -D_POSIX_C_SOURCE=200809L \
//        -fexceptions -fno-rtti -fvisibility=default -fPIC -shared \
//        -o libangle_throw_tracer.so libangle_throw_tracer.cc
//
// Run: LD_PRELOAD=libangle_throw_tracer.so ./cefsimple --use-gl=angle \
//        --use-angle=gles-egl ...
//
// What it does:
// 1. Interposes 4 EGL entry points: eglGetPlatformDisplay,
//    eglGetPlatformDisplayEXT, eglInitialize, eglTerminate.
// 2. Interposes __cxa_throw (libc++abi) to capture throw-site context at
//    throw time, when stack frames are still intact (post-unwind they are
//    destroyed).
// 3. On catch (in an EGL entry wrapper) of std::system_error, prints
//    call name + e.code() + e.what() to stderr, then ::_exit(1) (we cannot
//    return normally because -fno-exceptions intermediate frames do not run
//    RAII dtors, leaving the global EGL mutex leaked).
// 4. The __cxa_throw hook is a no-op for non-EGL throws (filter via
//    tls_egl_call_depth == 0 → forward silently).
// 5. First version outputs RAW return addresses (no symbolization in-hook
//    to avoid C++ runtime / dynamic loader dependencies). Symbolization
//    is post-mortem on the host via addr2line.
//
// Diagnostic-only, NOT a permanent implementation.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <unistd.h>     // write(2), ::_exit(2)
#include <dlfcn.h>       // dlsym(RTLD_NEXT, ...)
#include <pthread.h>     // pthread_self, pthread_key_t (unused for now)

// QNX EGL official headers (no self-defined types).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include <system_error>
#include <typeinfo>

// --------------------------------------------------------------------------
// Itanium C++ ABI: __cxa_throw signature
// QNX libc++abi declares in <cxxabi.h>:
//   void __cxa_throw(void* thrown_exception, std::type_info* tinfo,
//                     void (_GLIBCXX_CDTOR_CALLABI*)(void*)) __attribute__((__noreturn__));
// We use void* for typeinfo to avoid pulling full std::type_info header.
// We do NOT add noexcept/noreturn ourselves: vary by impl.
// --------------------------------------------------------------------------
typedef void (*cxa_dtor_t)(void*);

// Real function pointers (resolved in constructor).
static void (*s_real_cxa_throw)(void*, void*, cxa_dtor_t) = nullptr;
static EGLDisplay (*s_real_eglGetPlatformDisplay)(EGLenum, void*, const EGLAttrib*) = nullptr;
static EGLDisplay (*s_real_eglGetPlatformDisplayEXT)(EGLenum, void*, const EGLint*) = nullptr;
static EGLBoolean (*s_real_eglInitialize)(EGLDisplay, EGLint*, EGLint*) = nullptr;
static EGLBoolean (*s_real_eglTerminate)(EGLDisplay) = nullptr;

// --------------------------------------------------------------------------
// EGL call context (thread-local stack).
// tls_egl_call_depth > 0 means we are inside an EGL entry wrapper.
// tls_egl_call_stack[i] is the i-th EGL call name (innermost at depth-1).
// --------------------------------------------------------------------------
static thread_local int tls_egl_call_depth = 0;
static thread_local const char* tls_egl_call_stack[8] = {nullptr};

static const char* tls_top_egl_call() {
    if (tls_egl_call_depth <= 0) return nullptr;
    return tls_egl_call_stack[tls_egl_call_depth - 1];
}

static void tls_push_egl_call(const char* name) {
    if (tls_egl_call_depth < (int)(sizeof(tls_egl_call_stack) / sizeof(tls_egl_call_stack[0]))) {
        tls_egl_call_stack[tls_egl_call_depth++] = name;
    }
    // depth == stack size: ASSERT-grade overflow, but we keep it graceful.
}

static void tls_pop_egl_call() {
    if (tls_egl_call_depth > 0) --tls_egl_call_depth;
}

// --------------------------------------------------------------------------
// Constructor: pre-resolve all real symbols via dlsym(RTLD_NEXT, ...).
// We resolve in constructor (not lazily in the hook) to avoid
//   - dlsym lock contention if called from inside the hook
//   - lazy initialization races (libc++abi may not be fully initialized
//     when the hook first fires)
// On any failure to resolve, we fall back to safe behavior:
//   - For EGL functions: pass-through null (or original behavior)
//   - For __cxa_throw: we can still interpose, but forwarding is null
//     (in that case just abort to avoid infinite recursion in real
//     __cxa_throw callers if they also call back to us)
// --------------------------------------------------------------------------
__attribute__((constructor))
static void libangle_throw_tracer_init() {
    s_real_cxa_throw = reinterpret_cast<decltype(s_real_cxa_throw)>(
        dlsym(RTLD_NEXT, "__cxa_throw"));
    s_real_eglGetPlatformDisplay = reinterpret_cast<decltype(s_real_eglGetPlatformDisplay)>(
        dlsym(RTLD_NEXT, "eglGetPlatformDisplay"));
    s_real_eglGetPlatformDisplayEXT = reinterpret_cast<decltype(s_real_eglGetPlatformDisplayEXT)>(
        dlsym(RTLD_NEXT, "eglGetPlatformDisplayEXT"));
    s_real_eglInitialize = reinterpret_cast<decltype(s_real_eglInitialize)>(
        dlsym(RTLD_NEXT, "eglInitialize"));
    s_real_eglTerminate = reinterpret_cast<decltype(s_real_eglTerminate)>(
        dlsym(RTLD_NEXT, "eglTerminate"));

    // Diagnostic: confirm the tracer was loaded by the dynamic linker.
    // We cannot call raw_write helpers (defined later). Use ::write directly.
    static const char msg[] =
        "[ANGLE-THROW-TRACER] libangle_throw_tracer.so LOADED\n";
    (void)!::write(2, msg, sizeof(msg) - 1);
}

// --------------------------------------------------------------------------
// raw_write helpers — POSIX write(2), no allocation, no stdio.
// --------------------------------------------------------------------------
static void raw_write(int fd, const char* s, size_t n) {
    while (n > 0) {
        ssize_t w = ::write(fd, s, n);
        if (w <= 0) break;
        s += w;
        n -= (size_t)w;
    }
}

static void raw_write_str(int fd, const char* s) {
    raw_write(fd, s, std::strlen(s));
}

// Emit uintptr_t as hex.
static void raw_write_hex(int fd, uintptr_t v) {
    char buf[19];  // "0x" + 16 nibbles + NUL
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; ++i) {
        int nibble = (int)((v >> ((15 - i) * 4)) & 0xf);
        buf[2 + i] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    raw_write(fd, buf, 18);
}

static void raw_write_u64(int fd, uint64_t v) {
    char buf[21];
    int n = 0;
    if (v == 0) { raw_write(fd, "0", 1); return; }
    while (v > 0 && n < 20) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    // reverse
    for (int i = 0; i < n/2; ++i) {
        char t = buf[i]; buf[i] = buf[n-1-i]; buf[n-1-i] = t;
    }
    raw_write(fd, buf, (size_t)n);
}

// --------------------------------------------------------------------------
// __cxa_throw hook.
//
// Q1-Q9 gate verified: QNX libc++abi's __cxa_throw is GLOBAL DEFAULT
// visibility, no Bsymbolic, no version script. LD_PRELOAD can interpose.
//
// Design constraints (DESIGN.md §4.2):
// - No allocation, no lock, no fprintf/std::cerr (use raw write(2))
// - TLS recursion guard (depth > 0 means we are already inside the hook;
//   forward silently to avoid infinite recursion)
// - EGL filter: if tls_egl_call_depth == 0, we are NOT in an EGL entry
//   path; forward silently (don't spam logs for unrelated std::system_error
//   throws elsewhere in the process)
//
// Output (raw): call name (if EGL context), throw site return address
// (raw uintptr_t), and 2 caller frames via __builtin_return_address(0..1).
// Host-side post-mortem symbolization is done via addr2line.
// --------------------------------------------------------------------------
extern "C" void __cxa_throw(void* thrown_object, void* tinfo, cxa_dtor_t destructor);

// TLS recursion guard for __cxa_throw.
static thread_local int tls_in_cxa_throw_depth = 0;

extern "C" void __cxa_throw(void* thrown_object, void* tinfo, cxa_dtor_t destructor) {
    // Recursion guard.
    if (tls_in_cxa_throw_depth > 0) {
        if (s_real_cxa_throw) s_real_cxa_throw(thrown_object, tinfo, destructor);
        __builtin_unreachable();
    }
    ++tls_in_cxa_throw_depth;

    // EGL filter: only log if we are in an EGL entry wrapper.
    // DIAGNOSTIC FINDING (2026-07-11): The std::system_error(EDEADLK) throw
    // during ANGLE --use-angle=gles-egl initialization occurs on a DIFFERENT
    // thread than the eglGetPlatformDisplay wrapper, so tls_egl_call_depth is
    // always 0 at throw time. The EGL catch in our wrapper never fires;
    // instead std::terminate is called. The throw originates from
    // libGLESv2.so in a std::set<string>::find call (ANGLE's TLS index map).
    // Keep the filter enabled; non-EGL throws should not spam logs.
    if (tls_egl_call_depth == 0) {
        // Not from an EGL path. Just forward silently.
        --tls_in_cxa_throw_depth;
        if (s_real_cxa_throw) s_real_cxa_throw(thrown_object, tinfo, destructor);
        __builtin_unreachable();
    }

    // Throw-site raw addresses (up to 2 caller frames).
    const char* call_name = tls_top_egl_call();
    void* ra0 = __builtin_return_address(0);
    void* ra1 = __builtin_return_address(1);

    raw_write_str(2, "[ANGLE-THROW-TRACER] __cxa_throw (raw) ");
    raw_write_hex(2, (uintptr_t)ra0);
    raw_write_str(2, " ");
    raw_write_hex(2, (uintptr_t)ra1);
    raw_write_str(2, "\n");
    if (call_name) {
        raw_write_str(2, "[ANGLE-THROW-TRACER]   egl-context=");
        raw_write_str(2, call_name);
        raw_write_str(2, "\n");
    }

    --tls_in_cxa_throw_depth;

    // Forward to libc++abi's real __cxa_throw.
    if (s_real_cxa_throw) {
        s_real_cxa_throw(thrown_object, tinfo, destructor);
    } else {
        // Couldn't resolve the real __cxa_throw in constructor. Abort to
        // avoid losing the exception silently.
        static const char m[] = "libangle_throw_tracer: real __cxa_throw unresolved, aborting\n";
        (void)!::write(2, m, sizeof(m) - 1);
        ::abort();
    }
    __builtin_unreachable();
}

// --------------------------------------------------------------------------
// EGL entry wrappers.
//
// Behavior:
// - Push call name to TLS stack
// - Call real function (resolved at constructor time)
// - Pop call name
// - If std::system_error thrown: print call + code + what, then ::_exit(1)
// - If any other exception: rethrow
//
// On exit code 1: ANGLE's ScopedGlobalEGLMutexLock RAII does not run
// in -fno-exceptions intermediate frames, leaving the global EGL mutex
// held. We cannot safely continue; the process must exit immediately.
// --------------------------------------------------------------------------

extern "C" EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                          const EGLAttrib* attrib_list) {
    if (!s_real_eglGetPlatformDisplay) {
        // Couldn't resolve; pass-through null
        return EGL_NO_DISPLAY;
    }
    tls_push_egl_call("eglGetPlatformDisplay");
    EGLDisplay result;
    try {
        result = s_real_eglGetPlatformDisplay(platform, native_display, attrib_list);
    } catch (const std::system_error& e) {
        raw_write_str(2, "[ANGLE-THROW-TRACER] catch in eglGetPlatformDisplay:\n");
        raw_write_str(2, "  e.code()=");
        raw_write_u64(2, (uint64_t)e.code().value());
        raw_write_str(2, "\n  e.what()=\"");
        raw_write_str(2, e.what());
        raw_write_str(2, "\"\n");
        // Intentional abnormal exit. ::_exit(2) is POSIX async-signal-safe
        // termination; atexit / static dtor / signal handler cleanup are
        // not run. We cannot return normally because the global EGL
        // mutex (ScopedGlobalEGLMutexLock RAII in -fno-exceptions
        // intermediate frames) is leaked.
        ::_exit(1);
    }
    tls_pop_egl_call();
    return result;
}

extern "C" EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void* native_display,
                                             const EGLint* attrib_list) {
    if (!s_real_eglGetPlatformDisplayEXT) {
        return EGL_NO_DISPLAY;
    }
    tls_push_egl_call("eglGetPlatformDisplayEXT");
    EGLDisplay result;
    try {
        result = s_real_eglGetPlatformDisplayEXT(platform, native_display, attrib_list);
    } catch (const std::system_error& e) {
        raw_write_str(2, "[ANGLE-THROW-TRACER] catch in eglGetPlatformDisplayEXT:\n");
        raw_write_str(2, "  e.code()=");
        raw_write_u64(2, (uint64_t)e.code().value());
        raw_write_str(2, "\n  e.what()=\"");
        raw_write_str(2, e.what());
        raw_write_str(2, "\"\n");
        ::_exit(1);
    }
    tls_pop_egl_call();
    return result;
}

extern "C" EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    if (!s_real_eglInitialize) {
        return EGLBoolean(0);
    }
    tls_push_egl_call("eglInitialize");
    EGLBoolean result;
    try {
        result = s_real_eglInitialize(dpy, major, minor);
    } catch (const std::system_error& e) {
        raw_write_str(2, "[ANGLE-THROW-TRACER] catch in eglInitialize:\n");
        raw_write_str(2, "  e.code()=");
        raw_write_u64(2, (uint64_t)e.code().value());
        raw_write_str(2, "\n  e.what()=\"");
        raw_write_str(2, e.what());
        raw_write_str(2, "\"\n");
        ::_exit(1);
    }
    tls_pop_egl_call();
    return result;
}

extern "C" EGLBoolean eglTerminate(EGLDisplay dpy) {
    if (!s_real_eglTerminate) {
        return EGLBoolean(0);
    }
    tls_push_egl_call("eglTerminate");
    EGLBoolean result;
    try {
        result = s_real_eglTerminate(dpy);
    } catch (const std::system_error& e) {
        raw_write_str(2, "[ANGLE-THROW-TRACER] catch in eglTerminate:\n");
        raw_write_str(2, "  e.code()=");
        raw_write_u64(2, (uint64_t)e.code().value());
        raw_write_str(2, "\n  e.what()=\"");
        raw_write_str(2, e.what());
        raw_write_str(2, "\"\n");
        ::_exit(1);
    }
    tls_pop_egl_call();
    return result;
}
