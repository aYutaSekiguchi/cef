// libangle_throw_tracer.cc — v4 (raw RA + dlopen-triggered MAP snapshot)
//
// Design:
//  - __cxa_throw hook is RAW only: pid/tid/depth/call + absolute RA per
//    frame, via _Unwind_Backtrace.  NO module table lookup, NO dladdr in
//    the hook body.  Data race with loader state = impossible.
//  - Constructor emits an initial MAP snapshot of the current process
//    via dl_iterate_phdr (single-threaded, safe at constructor time).
//  - dlopen() is interposed.  After real dlopen returns (normal context,
//    no loader lock held by us), we emit a fresh MAP snapshot tagged
//    with pid.  Host-side: the latest MAP snapshot for a given pid, at
//    or before a throw line, gives us the DSO range table to convert
//    absolute RA -> DSO basename + offset.
//  - dlopen recursion guard (loader may call dlopen internally).
//  - If real dlopen unresolved: emit fatal marker and ::abort().
//
// Build: q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -D_POSIX_C_SOURCE=200809L \
//    -fexceptions -fno-rtti -fvisibility=default -fPIC -shared \
//    -o libangle_throw_tracer.so libangle_throw_tracer.cc
//
// Run: LD_PRELOAD=libangle_throw_tracer.so <binary> [--use-gl=angle ...]
//
// Diagnostic-only.  Not a permanent implementation.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <unistd.h>     // write, _exit, getpid
#include <dlfcn.h>       // dlsym, dlopen, RTLD_NEXT, dl_iterate_phdr
#include <pthread.h>     // pthread_self
#include <sys/link.h>    // dl_iterate_phdr, struct dl_phdr_info

#include <unwind.h>      // _Unwind_Backtrace, _Unwind_GetIP

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include <system_error>
#include <typeinfo>

// ---------------------------------------------------------------------------
typedef void (*cxa_dtor_t)(void*);

static void  (*s_real_cxa_throw)(void*, void*, cxa_dtor_t) = nullptr;
static void* (*s_real_dlopen)   (const char*, int) = nullptr;

static EGLDisplay (*s_real_eglGetPlatformDisplay)(EGLenum, void*, const EGLAttrib*) = nullptr;
static EGLDisplay (*s_real_eglGetPlatformDisplayEXT)(EGLenum, void*, const EGLint*) = nullptr;
static EGLBoolean (*s_real_eglInitialize)(EGLDisplay, EGLint*, EGLint*) = nullptr;
static EGLBoolean (*s_real_eglTerminate)(EGLDisplay) = nullptr;

// ---------------------------------------------------------------------------
// pid (constant) + tid helper.
// ---------------------------------------------------------------------------
static pid_t s_pid = 0;
static inline pthread_t s_tid() { return pthread_self(); }

// ---------------------------------------------------------------------------
// EGL thread-local stack.
// ---------------------------------------------------------------------------
static thread_local int          tls_depth = 0;
static thread_local const char*  tls_stack[8] = {nullptr};
static const char* tls_top() { return tls_depth > 0 ? tls_stack[tls_depth-1] : nullptr; }
static void tls_push(const char* n) {
    if (tls_depth < (int)(sizeof(tls_stack)/sizeof(tls_stack[0]))) tls_stack[tls_depth++] = n;
}
static void tls_pop() { if (tls_depth > 0) --tls_depth; }

// ---------------------------------------------------------------------------
// raw_write helpers.
// (no direct fd emit helpers needed; all output goes through linebuf)

// ---------------------------------------------------------------------------
// Meta prefix removed — superseded by per-line lb_* helpers below.
// ---------------------------------------------------------------------------
// Snapshot id: atomic counter (one per snapshot emit).  Each MAP line and
// the BEGIN/END markers carry the same snapshot_id for that pid.  Host
// parser groups by snapshot_id.
//
// Per-line emit (no global mutable state): each MAP line is built into a
// stack-local buffer and emitted with a single write() (<= PIPE_BUF for
// atomicity on QNX).  Interleave between threads is allowed and expected;
// grouping is by snapshot_id.
// ---------------------------------------------------------------------------

// Per-thread recursive snapshot id (allows re-entry safety).  We allocate
// from a per-pid counter guarded by an atomic CAS loop.  snapshot id 0
// is reserved for the constructor snapshot.
static volatile uint32_t s_snap_id = 0;
static uint32_t next_snap_id() {
    return __sync_add_and_fetch(&s_snap_id, 1);
}

// Stack-local 384-byte line buffer (kept well under PIPE_BUF=4096).
// Format: "[MAP] snap=N pid=P tid=T ..." followed by '\n'.
struct linebuf { char b[384]; int n; };
static void lb_init(linebuf* L) { L->n = 0; }
static void lb_w(linebuf* L, const char* s, int n) {
    // Caller guarantees L->n + n <= sizeof(L->b).
    for (int i = 0; i < n; ++i) L->b[L->n++] = s[i];
}
static void lb_ws(linebuf* L, const char* s) { lb_w(L, s, (int)std::strlen(s)); }
static void lb_wx(linebuf* L, uintptr_t v) {
    char tmp[18]; tmp[0]='0';tmp[1]='x';
    for (int i = 0; i < 16; ++i) tmp[2+i]="0123456789abcdef"[(v>>((15-i)*4))&0xf];
    lb_w(L, tmp, 18);
}
static void lb_wu(linebuf* L, uint64_t v) {
    char tmp[21]; int n=0; if(!v){lb_w(L,"0",1);return;}
    while(v&&n<20){tmp[n++]=(char)('0'+(v%10));v/=10;}
    for(int i=0;i<n/2;++i){char t=tmp[i];tmp[i]=tmp[n-1-i];tmp[n-1-i]=t;}
    lb_w(L, tmp, n);
}
static void lb_snap_prefix(linebuf* L, uint32_t snap_id) {
    lb_ws(L, "[MAP] snap=");
    char s[12]; int sn=0; uint32_t x = snap_id;
    if (!x) { s[sn++]='0'; } else { while(x){s[sn++]=(char)('0'+(x%10));x/=10;} for(int i=0;i<sn/2;++i){char t=s[i];s[i]=s[sn-1-i];s[sn-1-i]=t;} }
    lb_w(L, s, sn);
    lb_ws(L, " pid="); lb_wu(L, (uint64_t)(int64_t)s_pid);
    lb_ws(L, " tid="); lb_wx(L, (uintptr_t)s_tid());
}
static void lb_emit(linebuf* L) {
    lb_w(L, "\n", 1);
    (void)!::write(2, L->b, (size_t)L->n);
    L->n = 0;
}

// Callback builds one line per PT_LOAD (and one base line per DSO).
// Truncates long names (>200 bytes) by writing a truncated marker.
static int emit_map_cb(const struct dl_phdr_info* info, size_t /*size*/, void* data) {
    uint32_t snap = *(uint32_t*)data;
    linebuf L; lb_init(&L);
    lb_snap_prefix(&L, snap);
    lb_ws(&L, "  name=");
    const char* nm = info->dlpi_name ? info->dlpi_name : "?";
    int nlen = (int)std::strlen(nm);
    if (nlen > 200) { lb_ws(&L, "[truncated]"); nlen = 0; }
    else lb_w(&L, nm, nlen);
    lb_ws(&L, "  base="); lb_wx(&L, (uintptr_t)info->dlpi_addr);
    lb_emit(&L);
    for (Elf64_Half i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == 1 /*PT_LOAD*/) {
            uintptr_t base = (uintptr_t)info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
            uintptr_t end  = base + info->dlpi_phdr[i].p_memsz;
            linebuf S; lb_init(&S);
            lb_snap_prefix(&S, snap);
            lb_ws(&S, "    seg base="); lb_wx(&S, base);
            lb_ws(&S, " end="); lb_wx(&S, end);
            lb_ws(&S, " filesz="); lb_wx(&S, (uintptr_t)info->dlpi_phdr[i].p_filesz);
            lb_emit(&S);
        }
    }
    return 0;
}

// Emit BEGIN + iterate + END.  All lines carry the same snap_id.
static void emit_map_snapshot(const char* why) {
    uint32_t snap = next_snap_id();
    {
        linebuf L; lb_init(&L);
        lb_snap_prefix(&L, snap);
        lb_ws(&L, "  BEGIN reason="); lb_ws(&L, why);
        lb_emit(&L);
    }
    dl_iterate_phdr(emit_map_cb, &snap);
    {
        linebuf L; lb_init(&L);
        lb_snap_prefix(&L, snap);
        lb_ws(&L, "  END");
        lb_emit(&L);
    }
}

// ---------------------------------------------------------------------------
// Constructor.  Single-threaded at load time -> safe to emit initial map.
// ---------------------------------------------------------------------------
__attribute__((constructor))
static void init() {
    s_pid = ::getpid();
    s_real_cxa_throw = (decltype(s_real_cxa_throw)) dlsym(RTLD_NEXT, "__cxa_throw");
    s_real_dlopen    = (decltype(s_real_dlopen))    dlsym(RTLD_NEXT, "dlopen");
    s_real_eglGetPlatformDisplay    = (decltype(s_real_eglGetPlatformDisplay))    dlsym(RTLD_NEXT, "eglGetPlatformDisplay");
    s_real_eglGetPlatformDisplayEXT = (decltype(s_real_eglGetPlatformDisplayEXT)) dlsym(RTLD_NEXT, "eglGetPlatformDisplayEXT");
    s_real_eglInitialize            = (decltype(s_real_eglInitialize))            dlsym(RTLD_NEXT, "eglInitialize");
    s_real_eglTerminate             = (decltype(s_real_eglTerminate))             dlsym(RTLD_NEXT, "eglTerminate");

    emit_map_snapshot("constructor");
    linebuf L; lb_init(&L);
    lb_ws(&L, "[TRACER] snap=0 pid="); lb_wu(&L, (uint64_t)(int64_t)s_pid);
    lb_ws(&L, " tid="); lb_wx(&L, (uintptr_t)s_tid());
    lb_ws(&L, " libangle_throw_tracer.so LOADED");
    lb_emit(&L);
}

// ---------------------------------------------------------------------------
// _Unwind_Backtrace callback.
// ---------------------------------------------------------------------------
#define MAX_FR 32
struct bt_ctx { uintptr_t ip[MAX_FR]; int n; };
static _Unwind_Reason_Code bt_cb(_Unwind_Context* ctx, void* arg) {
    bt_ctx* c = (bt_ctx*)arg;
    if (c->n < MAX_FR) { c->ip[c->n++] = _Unwind_GetIP(ctx); return _URC_NO_REASON; }
    return _URC_END_OF_STACK;
}

// ---------------------------------------------------------------------------
// __cxa_throw hook — RAW only.  No module table lookup.  No dladdr.
// ---------------------------------------------------------------------------
extern "C" void __cxa_throw(void* obj, void* ti, cxa_dtor_t dtor);
static thread_local int tls_cxa = 0;

extern "C" void __cxa_throw(void* obj, void* ti, cxa_dtor_t dtor) {
    if (tls_cxa > 0) { if(s_real_cxa_throw) s_real_cxa_throw(obj,ti,dtor); __builtin_unreachable(); }
    ++tls_cxa;

    // Header line: [TRACER] snap=0 pid=... tid=... depth=... call=... (no module table)
    {
        linebuf L; lb_init(&L);
        lb_ws(&L, "[TRACER] snap=0 pid="); lb_wu(&L, (uint64_t)(int64_t)s_pid);
        lb_ws(&L, " tid="); lb_wx(&L, (uintptr_t)s_tid());
        lb_ws(&L, " __cxa_throw depth="); lb_wu(&L, (uint64_t)tls_depth);
        lb_ws(&L, " call="); lb_ws(&L, tls_top() ? tls_top() : "(none)");
        lb_emit(&L);
    }

    bt_ctx bt; bt.n = 0;
    _Unwind_Backtrace(bt_cb, &bt);

    for (int i = 0; i < bt.n; ++i) {
        linebuf L; lb_init(&L);
        lb_ws(&L, "[TRACER] snap=0 pid="); lb_wu(&L, (uint64_t)(int64_t)s_pid);
        lb_ws(&L, " tid="); lb_wx(&L, (uintptr_t)s_tid());
        lb_ws(&L, " ra["); lb_wu(&L, (uint64_t)i);
        lb_ws(&L, "]="); lb_wx(&L, bt.ip[i]);
        lb_emit(&L);
    }

    --tls_cxa;
    if (s_real_cxa_throw) { s_real_cxa_throw(obj, ti, dtor); }
    else { static const char m[]="[TRACER] FATAL: real __cxa_throw unresolved\n"; (void)!::write(2,m,sizeof(m)-1); ::abort(); }
    __builtin_unreachable();
}

// ---------------------------------------------------------------------------
// dlopen interpose.  Recursion guard.  Emit MAP snapshot after real dlopen.
// ---------------------------------------------------------------------------
static thread_local int tls_dlop = 0;

extern "C" void* dlopen(const char* pathname, int mode) {
    // Re-entry guard: loader may call dlopen internally; do not recurse.
    if (tls_dlop > 0) {
        if (!s_real_dlopen) return nullptr;
        return s_real_dlopen(pathname, mode);
    }
    if (!s_real_dlopen) {
        static const char m[] = "[TRACER] FATAL: real dlopen unresolved, aborting\n";
        (void)!::write(2, m, sizeof(m)-1);
        ::abort();
    }
    ++tls_dlop;
    void* r = s_real_dlopen(pathname, mode);
    --tls_dlop;
    if (r) {
        // Normal context, no loader lock held -> safe to walk.
        emit_map_snapshot("dlopen-returned-non-null");
    }
    return r;
}

// ---------------------------------------------------------------------------
// EGL wrap helpers — entry/exit logging with pid/tid (line-local emit).
// ---------------------------------------------------------------------------
static void egl_catch_die(const char* call, const std::system_error& e) {
    linebuf L; lb_init(&L);
    lb_ws(&L, "[TRACER] snap=0 pid="); lb_wu(&L, (uint64_t)(int64_t)s_pid);
    lb_ws(&L, " tid="); lb_wx(&L, (uintptr_t)s_tid());
    lb_ws(&L, " EGL catch call="); lb_ws(&L, call);
    lb_ws(&L, " e.code="); lb_wu(&L, (uint64_t)e.code().value());
    lb_ws(&L, " e.what=\""); lb_ws(&L, e.what()); lb_ws(&L, "\"");
    lb_emit(&L);
    ::_exit(1);
}

#define EGL_BEGIN(c) do {                                                \
    {                                                                    \
        linebuf _L; lb_init(&_L);                                        \
        lb_ws(&_L, "[TRACER] snap=0 pid="); lb_wu(&_L, (uint64_t)(int64_t)s_pid); \
        lb_ws(&_L, " tid="); lb_wx(&_L, (uintptr_t)s_tid());              \
        lb_ws(&_L, " EGL > " c);                                         \
        lb_ws(&_L, " depth="); lb_wu(&_L, (uint64_t)tls_depth);           \
        lb_emit(&_L);                                                    \
    }                                                                    \
    tls_push(c);                                                         \
} while (0)

#define EGL_END(c2) do {                                                 \
    tls_pop();                                                           \
    {                                                                    \
        linebuf _L; lb_init(&_L);                                        \
        lb_ws(&_L, "[TRACER] snap=0 pid="); lb_wu(&_L, (uint64_t)(int64_t)s_pid); \
        lb_ws(&_L, " tid="); lb_wx(&_L, (uintptr_t)s_tid());              \
        lb_ws(&_L, " EGL < " c2);                                        \
        lb_ws(&_L, " depth="); lb_wu(&_L, (uint64_t)tls_depth);           \
        lb_emit(&_L);                                                    \
    }                                                                    \
} while (0)

// ---------------------------------------------------------------------------
// EGL entry wrappers.
// ---------------------------------------------------------------------------
extern "C" EGLDisplay eglGetPlatformDisplay(EGLenum pl, void* nd, const EGLAttrib* al) {
    if (!s_real_eglGetPlatformDisplay) return EGL_NO_DISPLAY;
    EGL_BEGIN("eglGetPlatformDisplay");
    EGLDisplay r; try { r=s_real_eglGetPlatformDisplay(pl,nd,al); } catch(const std::system_error& e){ egl_catch_die("eglGetPlatformDisplay",e); }
    EGL_END("eglGetPlatformDisplay"); return r;
}
extern "C" EGLDisplay eglGetPlatformDisplayEXT(EGLenum pl, void* nd, const EGLint* al) {
    if (!s_real_eglGetPlatformDisplayEXT) return EGL_NO_DISPLAY;
    EGL_BEGIN("eglGetPlatformDisplayEXT");
    EGLDisplay r; try { r=s_real_eglGetPlatformDisplayEXT(pl,nd,al); } catch(const std::system_error& e){ egl_catch_die("eglGetPlatformDisplayEXT",e); }
    EGL_END("eglGetPlatformDisplayEXT"); return r;
}
extern "C" EGLBoolean eglInitialize(EGLDisplay d, EGLint* ma, EGLint* mi) {
    if (!s_real_eglInitialize) return EGLBoolean(0);
    EGL_BEGIN("eglInitialize");
    EGLBoolean r; try { r=s_real_eglInitialize(d,ma,mi); } catch(const std::system_error& e){ egl_catch_die("eglInitialize",e); }
    EGL_END("eglInitialize"); return r;
}
extern "C" EGLBoolean eglTerminate(EGLDisplay d) {
    if (!s_real_eglTerminate) return EGLBoolean(0);
    EGL_BEGIN("eglTerminate");
    EGLBoolean r; try { r=s_real_eglTerminate(d); } catch(const std::system_error& e){ egl_catch_die("eglTerminate",e); }
    EGL_END("eglTerminate"); return r;
}