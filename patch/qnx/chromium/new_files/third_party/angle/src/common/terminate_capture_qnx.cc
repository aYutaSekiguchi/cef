// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// terminate_capture_qnx.cc:
//   Captures a backtrace when std::terminate fires on QNX and prints it to
//   stderr before propagating the abort.  This is used to diagnose the
//   `std::system_error: mutex lock failed: Resource deadlock avoided` abort
//   that occurs when ANGLE initialization collides with non-recursive
//   std::mutex implementations during eglGetPlatformDisplay / eglInitialize
//   from the chromium GPU child process.
//
//   Installed via __attribute__((constructor)) so it is wired automatically
//   into any binary that links libEGL/libANGLE.
//
#if defined(__QNXNTO__)

#include <unwind.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

namespace angle
{
namespace qnx_terminate
{

constexpr int kMaxFrames = 96;

struct CapturedBacktrace
{
    int   depth = 0;
    void* frames[kMaxFrames];
};

static std::atomic<int> g_install_state{0};  // 0 = not installed, 1 = installed

_Unwind_Reason_Code TraceCallback(_Unwind_Context* ctx, void* arg)
{
    auto* trace = static_cast<CapturedBacktrace*>(arg);
    if (trace->depth < kMaxFrames)
    {
        trace->frames[trace->depth++] = reinterpret_cast<void*>(_Unwind_GetIP(ctx));
    }
    return _URC_NO_REASON;
}

void PrintBacktrace(const char* tag, const CapturedBacktrace& trace)
{
    std::fprintf(stderr, "[QNX-ANGLE-TRACE] %s backtrace (%d frames):\n", tag, trace->depth);
    for (int i = 0; i < trace->depth; ++i)
    {
        Dl_info info{};
        long offset = 0;
        const char* sym = "?";

        if (dladdr(trace.frames[i], &info) && info.dli_fname)
        {
            if (info.dli_sname)
            {
                sym = info.dli_sname;
                if (info.dli_saddr)
                {
                    offset = reinterpret_cast<char*>(trace.frames[i]) -
                             reinterpret_cast<char*>(info.dli_saddr);
                }
            }
            std::fprintf(stderr, "  #%02d 0x%p %s :: %s+0x%lx\n",
                         i, trace.frames[i], info.dli_fname, sym, offset);
        }
        else
        {
            std::fprintf(stderr, "  #%02d 0x%p <unknown>\n", i, trace.frames[i]);
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

    CapturedBacktrace trace;
    _Unwind_Backtrace(TraceCallback, &trace);
    PrintBacktrace("terminate", trace);

    try
    {
        std::rethrow_exception(std::current_exception());
    }
    catch (std::system_error& se)
    {
        std::fprintf(stderr,
                     "[QNX-ANGLE-TRACE] exception std::system_error code=%d category=\"%s\" what=\"%s\"\n",
                     static_cast<int>(se.code().value()),
                     se.code().category().name(),
                     se.what());
    }
    catch (std::exception& ex)
    {
        std::fprintf(stderr,
                     "[QNX-ANGLE-TRACE] exception std::exception what=\"%s\" type=\"%s\"\n",
                     ex.what(), typeid(ex).name());
    }
    catch (...)
    {
        std::fprintf(stderr, "[QNX-ANGLE-TRACE] exception unknown\n");
    }
    std::fflush(stderr);
}

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
