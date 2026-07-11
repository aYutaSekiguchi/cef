// global_mutex_qnx_diag.cc — QNX-limited GlobalMutex diagnostic instrumentation.
//
// Design (per supervisor review, 2026-07-11):
//   - No mutex semantics change. No catch, no recursive, no try_lock.
//   - TLS fixed-size table held[4] = {{this, count}, ...} tracks recursive
//     holdings of GlobalMutex on the current thread.
//   - lock entry:  lookup this in held[]; if found (count>0) emit REENTRY
//                 BEFORE mMutex.lock().
//   - lock success (after mMutex.lock returns): if no exception propagated,
//                 increment count or insert. If exception (EDEADLK throw
//                 inside mMutex.lock()), leave table unchanged.
//   - unlock entry/output: lookup this in held[]; emit count. After
//                 mMutex.unlock returns, count-- or remove.
//   - No owner guess. Only emit pid/tid/this/held_count as FACT.
//   - No slot name inference.
//   - Raw write(2), per line < PIPE_BUF. Allocation/lock forbidden.
//
// Build conditions:
//   - QNX only: #if defined(__QNXNTO__)
//   - Opt-in via: ANGLE_QNX_GLOBAL_MUTEX_DIAGNOSTIC

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <unistd.h>      // ::write
#include <pthread.h>     // pthread_self

#if defined(__QNXNTO__) && defined(ANGLE_QNX_GLOBAL_MUTEX_DIAGNOSTIC)

namespace angle_qnx_gm_diag
{

constexpr int HELD_SLOTS = 4;

struct HeldSlot {
    const void* m;  // GlobalMutex*
    int         n;  // recursive lock count
};

static thread_local HeldSlot tls_held[HELD_SLOTS];
static thread_local int      tls_held_used = 0;

static int find_slot(const void* m) {
    for (int i = 0; i < tls_held_used; ++i) {
        if (tls_held[i].m == m && tls_held[i].n > 0) return i;
    }
    return -1;
}

// Insert or increment.  Called after successful mMutex.lock().
static void insert_or_inc(const void* m) {
    int idx = find_slot(m);
    if (idx >= 0) {
        ++tls_held[idx].n;
        return;
    }
    if (tls_held_used < HELD_SLOTS) {
        tls_held[tls_held_used].m = m;
        tls_held[tls_held_used].n = 1;
        ++tls_held_used;
    }
    // Table overflow: silent (host parser sees no entry; still OK).
}

// Decrement or remove.  Called after successful mMutex.unlock().
static void dec_or_remove(const void* m) {
    int idx = find_slot(m);
    if (idx < 0) return;
    if (tls_held[idx].n > 1) {
        --tls_held[idx].n;
    } else {
        for (int j = idx; j + 1 < tls_held_used; ++j) {
            tls_held[j] = tls_held[j + 1];
        }
        --tls_held_used;
        tls_held[tls_held_used].m = nullptr;
        tls_held[tls_held_used].n = 0;
    }
}

static int held_count(const void* m) {
    int idx = find_slot(m);
    return idx >= 0 ? tls_held[idx].n : 0;
}

static int held_total() {
    int t = 0;
    for (int i = 0; i < tls_held_used; ++i) t += tls_held[i].n;
    return t;
}

// ---------------------------------------------------------------------------
// raw write helpers (per-line, < PIPE_BUF).
// ---------------------------------------------------------------------------
struct linebuf { char b[256]; int n; };
static void lb_init(linebuf* L) { L->n = 0; }
static void lb_w(linebuf* L, const char* s, int n) {
    for (int i = 0; i < n; ++i) L->b[L->n++] = s[i];
}
static void lb_ws(linebuf* L, const char* s) { lb_w(L, s, (int)std::strlen(s)); }
static void lb_wx(linebuf* L, uintptr_t v) {
    char tmp[18]; tmp[0]='0';tmp[1]='x';
    for (int i=0;i<16;++i) tmp[2+i]="0123456789abcdef"[(v>>((15-i)*4))&0xf];
    lb_w(L, tmp, 18);
}
static void lb_wu(linebuf* L, uint64_t v) {
    char tmp[21]; int n=0; if(!v){lb_w(L,"0",1);return;}
    while(v&&n<20){tmp[n++]=(char)('0'+(v%10));v/=10;}
    for(int i=0;i<n/2;++i){char t=tmp[i];tmp[i]=tmp[n-1-i];tmp[n-1-i]=t;}
    lb_w(L, tmp, n);
}
static void lb_pid_tid(linebuf* L) {
    lb_ws(L, " pid="); lb_wu(L, (uint64_t)(int64_t)::getpid());
    lb_ws(L, " tid="); lb_wx(L, (uintptr_t)pthread_self());
}
static void lb_emit(linebuf* L) {
    lb_w(L, "\n", 1);
    (void)!::write(2, L->b, (size_t)L->n);
    L->n = 0;
}

// ---------------------------------------------------------------------------
// Extern C entry points invoked from GlobalMutex.cpp.
// ---------------------------------------------------------------------------

__attribute__((noinline, used))
void diag_lock_entry(const void* gm_ptr) {
    int prev = held_count(gm_ptr);
    int total = held_total();
    linebuf L; lb_init(&L);
    lb_ws(&L, "[GMD]");
    lb_pid_tid(&L);
    lb_ws(&L, " event=lock-entry this="); lb_wx(&L, (uintptr_t)gm_ptr);
    lb_ws(&L, " was_held="); lb_wu(&L, (uint64_t)prev);
    lb_ws(&L, " total_held="); lb_wu(&L, (uint64_t)total);
    lb_emit(&L);
    if (prev > 0) {
        linebuf R; lb_init(&R);
        lb_ws(&R, "[GMD]");
        lb_pid_tid(&R);
        lb_ws(&R, " event=REENTRY this="); lb_wx(&R, (uintptr_t)gm_ptr);
        lb_ws(&R, " was_held="); lb_wu(&R, (uint64_t)prev);
        lb_emit(&R);
    }
}

__attribute__((noinline, used))
void diag_lock_success(const void* gm_ptr) {
    insert_or_inc(gm_ptr);
    int now = held_count(gm_ptr);
    int total = held_total();
    linebuf L; lb_init(&L);
    lb_ws(&L, "[GMD]");
    lb_pid_tid(&L);
    lb_ws(&L, " event=lock-ok this="); lb_wx(&L, (uintptr_t)gm_ptr);
    lb_ws(&L, " now_held="); lb_wu(&L, (uint64_t)now);
    lb_ws(&L, " total_held="); lb_wu(&L, (uint64_t)total);
    lb_emit(&L);
}

__attribute__((noinline, used))
void diag_unlock_entry(const void* gm_ptr) {
    int was = held_count(gm_ptr);
    int total = held_total();
    linebuf L; lb_init(&L);
    lb_ws(&L, "[GMD]");
    lb_pid_tid(&L);
    lb_ws(&L, " event=unlock-entry this="); lb_wx(&L, (uintptr_t)gm_ptr);
    lb_ws(&L, " was_held="); lb_wu(&L, (uint64_t)was);
    lb_ws(&L, " total_held="); lb_wu(&L, (uint64_t)total);
    lb_emit(&L);
}

__attribute__((noinline, used))
void diag_unlock_success(const void* gm_ptr) {
    dec_or_remove(gm_ptr);
    int total = held_total();
    linebuf L; lb_init(&L);
    lb_ws(&L, "[GMD]");
    lb_pid_tid(&L);
    lb_ws(&L, " event=unlock-ok this="); lb_wx(&L, (uintptr_t)gm_ptr);
    lb_ws(&L, " total_held="); lb_wu(&L, (uint64_t)total);
    lb_emit(&L);
}

}  // namespace angle_qnx_gm_diag

extern "C" void angle_qnx_gm_diag_lock_entry(const void* gm_ptr) {
    angle_qnx_gm_diag::diag_lock_entry(gm_ptr);
}
extern "C" void angle_qnx_gm_diag_lock_success(const void* gm_ptr) {
    angle_qnx_gm_diag::diag_lock_success(gm_ptr);
}
extern "C" void angle_qnx_gm_diag_unlock_entry(const void* gm_ptr) {
    angle_qnx_gm_diag::diag_unlock_entry(gm_ptr);
}
extern "C" void angle_qnx_gm_diag_unlock_success(const void* gm_ptr) {
    angle_qnx_gm_diag::diag_unlock_success(gm_ptr);
}

#endif  // __QNXNTO__ && ANGLE_QNX_GLOBAL_MUTEX_DIAGNOSTIC