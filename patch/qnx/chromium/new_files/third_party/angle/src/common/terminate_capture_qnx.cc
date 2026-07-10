// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// terminate_capture_qnx.cc:
//   Captures a backtrace when std::terminate fires on QNX and prints it
//   to stderr before propagating the abort.  Diagnoses the
//   `std::system_error: mutex lock failed: Resource deadlock avoided` abort
//   observed in the chromium GPU child process when ANGLE's EGL display
//   initialization collides with non-recursive std::mutex implementations.
//
//   The walk-order is the same as Chromium's
//   `base::debug::StackTrace::CollectStackTrace`:
//     1. Try libgcc_s `_Unwind_Backtrace` first (works pre-unwind).
//     2. Fall back to x86_64 frame-pointer walking (rbp chain).  This is
//        robust against QNX's libbacktrace library which cannot parse
//        the Chromium-shipped EH frames.
//   ANGLE's libangle_common builds with -fno-exceptions -fno-rtti so
//   we cannot `catch (...)`; the throw site is identified purely from
//   the backtrace.
//
#if defined(__QNXNTO__)

#if defined(__QNXNTO__)
#    if !defined(_QNX_SOURCE)
#        define _QNX_SOURCE 1
#    endif
#    if !defined(_GNU_SOURCE)
#        define _GNU_SOURCE 1
#    endif
#endif

#include <unwind.h>
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <typeinfo>

namespace angle
{
namespace qnx_terminate
{

constexpr int kMaxFrames = 96;
constexpr uintptr_t kMaxStackSize = 64u * 1024u * 1024u;  // 64 MB

// State passed through _Unwind_Backtrace trace callback.
struct UnwindState
{
    const void** frames;
    int depth;
};

_Unwind_Reason_Code TraceCallback(_Unwind_Context* ctx, void* arg)
{
    auto* state = static_cast<UnwindState*>(arg);
    if (state->depth < kMaxFrames)
    {
        state->frames[state->depth++] =
            reinterpret_cast<const void*>(_Unwind_GetIP(ctx));
    }
    return _URC_NO_REASON;
}

// Frame-pointer walker (x86_64 System V ABI).  Mirrors Chromium's
// base::debug::StackTrace fallback for QNX.
__attribute__((noinline, used))
static int WalkFramePointers(const void** out, int max_frames)
{
    if (max_frames <= 0)
    {
        return 0;
    }

    uintptr_t rbp = reinterpret_cast<uintptr_t>(
        __builtin_frame_address(0));
    if (rbp == 0)
    {
        return 0;
    }

    const uintptr_t stack_high = rbp;
    const uintptr_t stack_low =
        (stack_high > kMaxStackSize) ? (stack_high - kMaxStackSize) : 0u;

    int count = 0;
    for (int i = 0; i < kMaxFrames && count < max_frames; ++i)
    {
        if (rbp < stack_low || rbp > stack_high + kMaxStackSize)
        {
            break;
        }

        const uintptr_t* fp_words =
            reinterpret_cast<const uintptr_t*>(rbp);
        const uintptr_t next_rbp = fp_words[0];
        const uintptr_t ret_addr = fp_words[1];

        if (ret_addr == 0)
        {
            break;
        }

        out[count++] = reinterpret_cast<const void*>(ret_addr);

        if (next_rbp <= rbp)
        {
            break;
        }
        rbp = next_rbp;
    }
    return count;
}

void PrintBacktrace(const char* tag)
{
    const void* frames[kMaxFrames];
    int count = 0;

    // First try libgcc_s unwinding.
    {
        UnwindState state{frames, 0};
        _Unwind_Backtrace(TraceCallback, &state);
        count = state.depth;
    }

    // If libgcc yielded nothing useful (only the trivial current frame
    // or zero), fall back to walking the rbp chain.
    if (count <= 1)
    {
        int fp_count = WalkFramePointers(frames, kMaxFrames);
        if (fp_count > count)
        {
            count = fp_count;
        }
    }

    std::fprintf(stderr, "[QNX-ANGLE-TRACE] %s backtrace (%d frames):\n", tag, count);
    for (int i = 0; i < count; ++i)
    {
        Dl_info info{};
        long offset = 0;
        const char* sym = "?";

        if (dladdr(frames[i], &info) && info.dli_fname)
        {
            if (info.dli_sname)
            {
                sym = info.dli_sname;
                if (info.dli_saddr)
                {
                    offset = reinterpret_cast<const char*>(frames[i]) -
                             reinterpret_cast<const char*>(info.dli_saddr);
                }
            }
            std::fprintf(stderr, "  #%02d 0x%p %s :: %s+0x%lx\n",
                         i, frames[i], info.dli_fname, sym, offset);
        }
        else
        {
            std::fprintf(stderr, "  #%02d 0x%p <unknown>\n", i, frames[i]);
        }
    }
    std::fflush(stderr);
}

void QnxTerminateHandler()
{
    std::fprintf(stderr,
                 "[QNX-ANGLE-TRACE] std::terminate invoked (pid=%d tid=%d)\n",
                 static_cast<int>(getpid()),
                 static_cast<int>(gettid()));
    std::fflush(stderr);

    PrintBacktrace("terminate");

    // Chromium's libangle_common compiles with -fno-exceptions -fno-rtti,
    // so we cannot catch the in-flight exception by type.  Print a
    // generic marker; the backtrace is the primary diagnostic.
    std::exception_ptr eptr = std::current_exception();
    if (eptr)
    {
        std::fprintf(stderr,
                     "[QNX-ANGLE-TRACE] exception: in-flight (rethrow/catch "
                     "disabled; inspect backtrace above for throw site)\n");
    }
    else
    {
        std::fprintf(stderr, "[QNX-ANGLE-TRACE] exception: no current exception\n");
    }
    std::fflush(stderr);
}

static std::atomic<int> g_install_state{0};

void Install()
{
    int expected = 0;
    if (!g_install_state.compare_exchange_strong(expected, 1))
    {
        return;
    }
    std::set_terminate(&QnxTerminateHandler);
    std::fprintf(stderr,
                 "[QNX-ANGLE-TRACE] terminate handler installed (pid=%d)\n",
                 static_cast<int>(getpid()));
    std::fflush(stderr);
}

}  // namespace qnx_terminate
}  // namespace angle

extern "C" __attribute__((constructor)) void angle_qnx_install_terminate_capture(void)
{
    ::angle::qnx_terminate::Install();
}

#endif  // defined(__QNXNTO__)
