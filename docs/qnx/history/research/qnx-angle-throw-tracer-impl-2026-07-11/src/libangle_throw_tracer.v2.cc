// libangle_throw_tracer.cc — v2 (corrected diagnostic mode)
// QNX ANGLE EGL abort diagnostic tracer.
//
// Build: -fexceptions -fno-rtti -fvisibility=default -fPIC -shared
//   q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -D_POSIX_C_SOURCE=200809L \
//        -fexceptions -fno-rtti -fvisibility=default -fPIC -shared \
//        -o libangle_throw_tracer.so libangle_throw_tracer.cc
//
// Run: LD_PRELOAD=libangle_throw_tracer.so ./cefsimple --use-gl=angle \
//        --use-angle=gles-egl ...
//
// v2 changes (2026-07-11), per audit correction:
//   — Always log ALL __cxa_throw (EGL filter REMOVED; filter was hiding
//     the throw under investigation).
//   — Every log line carries pid + tid for same-run thread comparison.
//   — EGL wrapper entry/exit logs include pid/tid/call/depth.
//   — Constructor builds a lock-free module range table via
//     dl_iterate_phdr(3); __cxa_throw hook resolves RA → DSO basename
//     + offset without dladdr/dlsym/lock/allocation.
//   — When catch fires, e.code() + e.what() are recorded (only then).
//
// Design constraints (DESIGN.md §4.2, v2 update):
//   — No allocation, no lock, no fprintf in the hook body.
//   — TLS recursion guard.
//   — Module table built at constructor time (single-threaded); runtime
//     lookups are read-only (lock-free).
//   — RTTI not used; catch is on fixed type std::system_error&.
//
// Diagnostic-only, NOT a permanent implementation.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <unistd.h>     // write(2), ::_exit(2), getpid(2)
#include <dlfcn.h>       // dlsym(RTLD_NEXT, ...)
#include <pthread.h>     // pthread_self
#include <sys/link.h>     // dl_iterate_phdr, struct dl_phdr_info
#include <sys/elf.h>     // PT_LOAD (pulled in by <sys/link.h>)

// QNX EGL official headers (no self-defined types).
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include <system_error>
#include <typeinfo>

// --------------------------------------------------------------------------
// Itanium C++ ABI: __cxa_throw signature
// --------------------------------------------------------------------------
typedef void (*cxa_dtor_t)(void*);

// --------------------------------------------------------------------------
// Real function pointers (resolved in constructor via dlsym(RTLD_NEXT)).
// --------------------------------------------------------------------------
static void (*s_real_cxa_throw)(void*, void*, cxa_dtor_t) = nullptr;
static EGLDisplay (*s_real_eglGetPlatformDisplay)(EGLenum, void*, const EGLAttrib*) = nullptr;
static EGLDisplay (*s_real_eglGetPlatformDisplayEXT)(EGLenum, void*, const EGLint*) = nullptr;
static EGLBoolean (*s_real_eglInitialize)(EGLDisplay, EGLint*, EGLint*) = nullptr;
static EGLBoolean (*s_real_eglTerminate)(EGLDisplay) = nullptr;

// --------------------------------------------------------------------------
// Module range table — built once in constructor via dl_iterate_phdr.
// Lock-free: all entries written in constructor (single-threaded), only
// read at runtime.  No dladdr/dlsym/allocation in the hot path.
// --------------------------------------------------------------------------
struct mod_entry {
    uintptr_t       base;    // dlpi_addr (base load address)
    uintptr_t       end;     // base + max(PT_LOAD p_vaddr + p_memsz)
    const char*     name;    // pointer into dl_iterate_phdr info (persistent)
};

#define MAX_MODULES 256
static mod_entry  g_modules[MAX_MODULES];
static int        g_module_count = 0;

static int collect_module(const struct dl_phdr_info* info, size_t /*size*/, void* /*data*/) {
    if (g_module_count >= MAX_MODULES) return 1; // stop iteration
    uintptr_t base = (uintptr_t)info->dlpi_addr;
    uintptr_t end  = base;
    for (Elf64_Half i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            uintptr_t seg_end = base +
                                (uintptr_t)info->dlpi_phdr[i].p_vaddr +
                                (uintptr_t)info->dlpi_phdr[i].p_memsz;
            if (seg_end > end) end = seg_end;
        }
    }
    mod_entry* m = &g_modules[g_module_count++];
    m->base = base;
    m->end  = end;
    m->name = info->dlpi_name;  // pointer lives as long as DSO is loaded
    return 0;
}

// --------------------------------------------------------------------------
// Cache pid (constant per process) and tid helper (inline TLS read).
// --------------------------------------------------------------------------
static pid_t s_pid = 0;

static inline pthread_t s_tid() {
    return pthread_self();  // __tls()->__tid on QNX — no syscall
}

// --------------------------------------------------------------------------
// EGL call context (thread-local stack).
// tls_egl_call_depth > 0 means we are inside an EGL entry wrapper.
// --------------------------------------------------------------------------
static thread_local int          tls_egl_call_depth = 0;
static thread_local const char*  tls_egl_call_stack[8] = {nullptr};

static const char* tls_top_egl_call() {
    if (tls_egl_call_depth <= 0) return nullptr;
    return tls_egl_call_stack[tls_egl_call_depth - 1];
}

static void tls_push_egl_call(const char* name) {
    if (tls_egl_call_depth < (int)(sizeof(tls_egl_call_stack) /
                                   sizeof(tls_egl_call_stack[0]))) {
        tls_egl_call_stack[tls_egl_call_depth++] = name;
    }
}

static void tls_pop_egl_call() {
    if (tls_egl_call_depth > 0) --tls_egl_call_depth;
}

// --------------------------------------------------------------------------
// Constructor: resolve real symbols, build module table, cache pid.
// --------------------------------------------------------------------------
__attribute__((constructor))
static void libangle_throw_tracer_init() {
    s_pid = ::getpid();

    s_real_cxa_throw = reinterpret_cast<decltype(s_real_cxa_throw)>(
        dlsym(RTLD_NEXT, "__cxa_throw"));
    s_real_eglGetPlatformDisplay =
        reinterpret_cast<decltype(s_real_eglGetPlatformDisplay)>(
            dlsym(RTLD_NEXT, "eglGetPlatformDisplay"));
    s_real_eglGetPlatformDisplayEXT =
        reinterpret_cast<decltype(s_real_eglGetPlatformDisplayEXT)>(
            dlsym(RTLD_NEXT, "eglGetPlatformDisplayEXT"));
    s_real_eglInitialize = reinterpret_cast<decltype(s_real_eglInitialize)>(
        dlsym(RTLD_NEXT, "eglInitialize"));
    s_real_eglTerminate = reinterpret_cast<decltype(s_real_eglTerminate)>(
        dlsym(RTLD_NEXT, "eglTerminate"));

    // Build module range table.
    dl_iterate_phdr(collect_module, nullptr);

    // Confirm loading.  Cannot use raw_write helpers (defined below).
    static const char msg[] =
        "[TRACER] pid=        tid=        libangle_throw_tracer.so LOADED  modules=\n";
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

static void raw_write_hex(int fd, uintptr_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; ++i)
        buf[2 + i] = "0123456789abcdef"[(v >> ((15 - i) * 4)) & 0xf];
    raw_write(fd, buf, 18);
}

static void raw_write_u64(int fd, uint64_t v) {
    char buf[21];
    int n = 0;
    if (v == 0) { raw_write(fd, "0", 1); return; }
    while (v > 0 && n < 20) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n / 2; ++i) {
        char t = buf[i]; buf[i] = buf[n - 1 - i]; buf[n - 1 - i] = t;
    }
    raw_write(fd, buf, (size_t)n);
}

// --------------------------------------------------------------------------
// log_meta_prefix: "[TRACER] pid=NNNNNN tid=PPPPPPPP "
// tid is printed as raw hex (like a pointer) — no formatting dependencies.
// --------------------------------------------------------------------------
static void log_meta_prefix() {
    raw_write_str(2, "[TRACER] pid=");
    raw_write_u64(2, (uint64_t)(int64_t)s_pid);
    raw_write_str(2, " tid=");
    raw_write_hex(2, (uintptr_t)s_tid());
    raw_write_str(2, " ");
}

// --------------------------------------------------------------------------
// Look up RA in the module table, return basename of the containing DSO.
// Returns "?" if not found.  Also computes RA offset from the DSO base.
// --------------------------------------------------------------------------
static const char* lookup_dso(uintptr_t ra, uintptr_t* out_offset) {
    for (int i = 0; i < g_module_count; ++i) {
        if (ra >= g_modules[i].base && ra < g_modules[i].end) {
            *out_offset = ra - g_modules[i].base;
            // Extract basename from full path (find last '/').
            const char* name = g_modules[i].name;
            if (!name || !name[0]) {
                *out_offset = ra; // no base adjustment for main executable
                return "?";
            }
            const char* slash = nullptr;
            for (const char* p = name; *p; ++p)
                if (*p == '/') slash = p;
            return slash ? slash + 1 : name;
        }
    }
    *out_offset = ra; // absolute — couldn't resolve
    return "?";
}

// --------------------------------------------------------------------------
// __cxa_throw hook — v2: ALWAYS log (no EGL filter).
//
// Output (one line per throw):
//   [TRACER] pid=N tid=NNNN __cxa_throw dso=NAME+0xOFF ra1=0x... depth=N call=NAME
// --------------------------------------------------------------------------
extern "C" void __cxa_throw(void* thrown_object, void* tinfo, cxa_dtor_t destructor);
static thread_local int tls_in_cxa_throw_depth = 0;

extern "C" void __cxa_throw(void* thrown_object, void* tinfo, cxa_dtor_t destructor) {
    if (tls_in_cxa_throw_depth > 0) {
        if (s_real_cxa_throw) s_real_cxa_throw(thrown_object, tinfo, destructor);
        __builtin_unreachable();
    }
    ++tls_in_cxa_throw_depth;

    void* ra0 = __builtin_return_address(0);
    void* ra1 = __builtin_return_address(1);
    uintptr_t off0 = 0, off1 = 0;
    const char* dso0 = lookup_dso((uintptr_t)ra0, &off0);
    const char* dso1 = lookup_dso((uintptr_t)ra1, &off1);
    int depth = tls_egl_call_depth;
    const char* call = tls_top_egl_call();

    log_meta_prefix();
    raw_write_str(2, "__cxa_throw  dso=");
    raw_write_str(2, dso0);
    raw_write_str(2, "+");
    raw_write_hex(2, off0);
    raw_write_str(2, "  ra1=");
    raw_write_str(2, dso1);
    raw_write_str(2, "+");
    raw_write_hex(2, off1);
    raw_write_str(2, "  depth=");
    raw_write_u64(2, (uint64_t)depth);
    raw_write_str(2, "  call=");
    raw_write_str(2, call ? call : "(none)");
    raw_write_str(2, "\n");

    --tls_in_cxa_throw_depth;

    if (s_real_cxa_throw) {
        s_real_cxa_throw(thrown_object, tinfo, destructor);
    } else {
        static const char m[] = "[TRACER] FATAL: real __cxa_throw unresolved\n";
        (void)!::write(2, m, sizeof(m) - 1);
        ::abort();
    }
    __builtin_unreachable();
}

// --------------------------------------------------------------------------
// EGL entry wrapper helper — logs entry and exit.
// --------------------------------------------------------------------------
#define EGL_WRAP_BEGIN(call_name)                                           \
    /* ENTRY */                                                             \
    log_meta_prefix();                                                      \
    raw_write_str(2, "EGL > " call_name);                                   \
    raw_write_str(2, "  depth=");                                           \
    raw_write_u64(2, (uint64_t)tls_egl_call_depth);                         \
    raw_write_str(2, "\n");                                                 \
    tls_push_egl_call(call_name);

#define EGL_WRAP_END(result_expr, fmt_fn)                                   \
    /* EXIT */                                                              \
    tls_pop_egl_call();                                                     \
    log_meta_prefix();                                                      \
    raw_write_str(2, "EGL < ");                                             \
    raw_write_str(2, tls_top_egl_call()                                     \
                         ? tls_top_egl_call() : "(none)");                  \
    raw_write_str(2, "  ret=0x");                                           \
    raw_write_hex(2, (uintptr_t)fmt_fn);                                    \
    raw_write_str(2, "  depth=");                                           \
    raw_write_u64(2, (uint64_t)tls_egl_call_depth);                         \
    raw_write_str(2, "\n");

// Catch helper
static void egl_catch_and_exit(const char* call_name, const std::system_error& e) {
    log_meta_prefix();
    raw_write_str(2, "EGL catch  call=");
    raw_write_str(2, call_name);
    raw_write_str(2, "  e.code()=");
    raw_write_u64(2, (uint64_t)e.code().value());
    raw_write_str(2, "  e.what()=\"");
    raw_write_str(2, e.what());
    raw_write_str(2, "\"\n");
    ::_exit(1);
}

// --------------------------------------------------------------------------
// EGL entry wrappers (v2: entry/exit logging with pid/tid).
// --------------------------------------------------------------------------

extern "C" EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                            const EGLAttrib* attrib_list) {
    if (!s_real_eglGetPlatformDisplay) return EGL_NO_DISPLAY;
    EGL_WRAP_BEGIN("eglGetPlatformDisplay")
    EGLDisplay result;
    try { result = s_real_eglGetPlatformDisplay(platform, native_display, attrib_list); }
    catch (const std::system_error& e) { egl_catch_and_exit("eglGetPlatformDisplay", e); }
    EGL_WRAP_END(result, (uintptr_t)result)
    return result;
}

extern "C" EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void* native_display,
                                               const EGLint* attrib_list) {
    if (!s_real_eglGetPlatformDisplayEXT) return EGL_NO_DISPLAY;
    EGL_WRAP_BEGIN("eglGetPlatformDisplayEXT")
    EGLDisplay result;
    try { result = s_real_eglGetPlatformDisplayEXT(platform, native_display, attrib_list); }
    catch (const std::system_error& e) { egl_catch_and_exit("eglGetPlatformDisplayEXT", e); }
    EGL_WRAP_END(result, (uintptr_t)result)
    return result;
}

extern "C" EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    if (!s_real_eglInitialize) return EGLBoolean(0);
    EGL_WRAP_BEGIN("eglInitialize")
    EGLBoolean result;
    try { result = s_real_eglInitialize(dpy, major, minor); }
    catch (const std::system_error& e) { egl_catch_and_exit("eglInitialize", e); }
    EGL_WRAP_END(result, (int)result)
    return result;
}

extern "C" EGLBoolean eglTerminate(EGLDisplay dpy) {
    if (!s_real_eglTerminate) return EGLBoolean(0);
    EGL_WRAP_BEGIN("eglTerminate")
    EGLBoolean result;
    try { result = s_real_eglTerminate(dpy); }
    catch (const std::system_error& e) { egl_catch_and_exit("eglTerminate", e); }
    EGL_WRAP_END(result, (int)result)
    return result;
}
