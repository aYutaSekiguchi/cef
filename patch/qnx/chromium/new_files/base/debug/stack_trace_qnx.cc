// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// QNX-specific stack trace implementation using the QNX backtrace API
// (<backtrace.h>, libbacktrace.so).
//
// On QNX SDP 8, glibc's <execinfo.h> and backtrace() are not available.
// Instead, QNX provides bt_get_backtrace() in <backtrace.h>.
// btl_get_backtrace() is declared but not implemented in SDP 8.
//
// Symbol resolution uses dladdr() which is available in QNX's <dlfcn.h>, plus
// abi::__cxa_demangle() from libc++abi's <cxxabi.h> for C++ demangling.

#include "base/debug/stack_trace.h"

#include <cxxabi.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include <backtrace.h>

#include <algorithm>
#include <ostream>
#include <string>

#include "base/compiler_specific.h"
#include "base/containers/span_writer.h"
#include "base/debug/debugger.h"
#include "base/memory/free_deleter.h"
#include "base/numerics/safe_conversions.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/cstring_view.h"
#include "build/build_config.h"

namespace base::debug {

namespace {

// ── Async-signal state ────────────────────────────────────────────────────────

// Set to 1 while inside StackDumpSignalHandler so CollectStackTrace can
// avoid heap-allocating symbol resolution.
volatile sig_atomic_t in_signal_handler = 0;

bool (*try_handle_signal)(int, siginfo_t*, void*) = nullptr;

// ── QNX bt_accessor_t singletons ─────────────────────────────────────────────
//
// bt_init_accessor() uses pthread_once internally; initialise once at program
// start via WarmUpBacktrace() so that the first real call is allocation-free.

bt_accessor_t& GetBtAccSelf() {
  static bt_accessor_t acc = []() {
    bt_accessor_t a = {};
    bt_init_accessor(&a, BT_SELF, 0);
    return a;
  }();
  return acc;
}

// ── Output helpers (async-signal safe) ───────────────────────────────────────

void PrintToStderr(const char* output) {
  std::ignore = HANDLE_EINTR(write(STDERR_FILENO, output, strlen(output)));
}

// Write |value| as a decimal string to stderr using only signal-safe ops.
[[maybe_unused]] void PrintToStderrInt(intptr_t value) {
  char buf[32];
  internal::itoa_r(value, 10, 0, buf);
  PrintToStderr(buf);
}

// Write |pointer| as "0x<hex>" to stderr.
[[maybe_unused]] void PrintToStderrPointer(const void* pointer) {
  char buf[17] = {'\0'};
  PrintToStderr("0x");
  internal::itoa_r(reinterpret_cast<intptr_t>(pointer), 16, 12, buf);
  PrintToStderr(buf);
}

// ── Output handler abstraction ────────────────────────────────────────────────

class BacktraceOutputHandler {
 public:
  virtual void HandleOutput(const char* output) = 0;
 protected:
  virtual ~BacktraceOutputHandler() = default;
};

class PrintBacktraceOutputHandler : public BacktraceOutputHandler {
 public:
  void HandleOutput(const char* output) override { PrintToStderr(output); }
};

class StreamBacktraceOutputHandler : public BacktraceOutputHandler {
 public:
  explicit StreamBacktraceOutputHandler(std::ostream* os) : os_(os) {}
  void HandleOutput(const char* output) override { (*os_) << output; }
 private:
  raw_ptr<std::ostream> os_;
};

// ── Symbol resolution (not async-signal safe, uses malloc) ───────────────────

// Attempt to resolve |addr| to a human-readable symbol using dladdr() and
// abi::__cxa_demangle().  Falls back to the raw address on failure.
// Must NOT be called from a signal handler.
std::string SymbolizeAddress(const void* addr) {
  Dl_info info;
  if (dladdr(addr, &info) != 0) {
    // Subtract 1 from the return address so that addr points into the call
    // instruction rather than the first byte of the next instruction.
    const char* name = info.dli_sname ? info.dli_sname : "<unknown>";

    // Try C++ demangling.
    int status = 0;
    std::unique_ptr<char, FreeDeleter> demangled(
        abi::__cxa_demangle(name, nullptr, nullptr, &status));
    if (status == 0 && demangled) {
      return std::string(demangled.get());
    }
    return std::string(name);
  }
  // No symbol info: fall back to hex address.
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", addr);
  return std::string(buf);
}

// ── ProcessBacktrace ──────────────────────────────────────────────────────────

void ProcessBacktrace(span<const void* const> traces,
                      cstring_view prefix_string,
                      BacktraceOutputHandler* handler) {
  // NOTE: When called from the signal handler (in_signal_handler == 1) this
  // function must be async-signal safe — no malloc, no stdio.  In that case
  // we print addresses only.  When called normally we also resolve symbols.

  traces = traces.first(std::min(traces.size(), StackTrace::kMaxTraces));

  const bool in_handler = (in_signal_handler != 0);

  for (size_t i = 0; i < traces.size(); ++i) {
    if (!prefix_string.empty()) {
      handler->HandleOutput(prefix_string.c_str());
    }

    // Frame index.
    char frame_buf[8] = {'\0'};
    handler->HandleOutput("#");
    internal::itoa_r(static_cast<intptr_t>(i), 10, 0, frame_buf);
    handler->HandleOutput(frame_buf);
    handler->HandleOutput(" ");

    // Address.
    char addr_buf[17] = {'\0'};
    handler->HandleOutput("0x");
    internal::itoa_r(reinterpret_cast<intptr_t>(traces[i]), 16, 12, addr_buf);
    handler->HandleOutput(addr_buf);

    if (!in_handler) {
      // Symbol name (heap-allocating path, not async-signal safe).
      handler->HandleOutput(" ");
      // Subtract 1 byte so we look up inside the call instruction.
      const void* lookup_addr =
          reinterpret_cast<const void*>(
              reinterpret_cast<uintptr_t>(traces[i]) - 1u);
      std::string sym = SymbolizeAddress(lookup_addr);
      handler->HandleOutput(sym.c_str());
    }

    handler->HandleOutput("\n");
  }
}

// ── Signal handler ────────────────────────────────────────────────────────────

void PrintSymbolForAddress(const void* address) {
  // Best-effort diagnostic for QNX bring-up. dladdr() is not guaranteed to be
  // async-signal safe, but without it QNX's bt_get_backtrace() can return no
  // frames from the signal handler, leaving only the faulting address. Keep this
  // simple: no demangling and no heap-owned strings.
  Dl_info dlinfo;
  if (dladdr(address, &dlinfo) == 0) {
    return;
  }

  PrintToStderr(dlinfo.dli_fname ? dlinfo.dli_fname : "<unknown object>");
  PrintToStderr(" ");
  PrintToStderr(dlinfo.dli_sname ? dlinfo.dli_sname : "<unknown symbol>");
  PrintToStderr(" + ");
  const uintptr_t offset = dlinfo.dli_saddr
      ? reinterpret_cast<uintptr_t>(address) -
            reinterpret_cast<uintptr_t>(dlinfo.dli_saddr)
      : 0;
  PrintToStderrPointer(reinterpret_cast<const void*>(offset));
}

void PrintFramePointerBacktrace(const void* stack_pointer,
                                const void* frame_pointer) {
#if defined(__x86_64__) || defined(__X86_64__)
  uintptr_t sp = reinterpret_cast<uintptr_t>(stack_pointer);
  uintptr_t fp = reinterpret_cast<uintptr_t>(frame_pointer);
  if (fp == 0 || fp < sp || fp - sp > (16u * 1024u * 1024u)) {
    return;
  }

  PrintToStderr("==== frame-pointer stack ====\n");
  for (int frame = 0; frame < 32; ++frame) {
    const uintptr_t* fp_words = reinterpret_cast<const uintptr_t*>(fp);
    const uintptr_t next_fp = fp_words[0];
    const uintptr_t return_address = fp_words[1];
    if (return_address == 0) {
      break;
    }

    PrintToStderr("#");
    PrintToStderrInt(frame);
    PrintToStderr(" ");
    const void* address = reinterpret_cast<const void*>(return_address);
    PrintToStderrPointer(address);
    PrintToStderr(" ");
    PrintSymbolForAddress(address);
    PrintToStderr("\n");

    if (next_fp <= fp || next_fp - sp > (16u * 1024u * 1024u)) {
      break;
    }
    fp = next_fp;
  }
#endif
}

void PrintCrashContext(void* void_context) {
  if (!void_context) {
    return;
  }

  ucontext_t* context = static_cast<ucontext_t*>(void_context);
#if defined(__x86_64__) || defined(__X86_64__)
  const void* ip = reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rip));
  const void* sp = reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rsp));
  const void* bp = reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rbp));
  PrintToStderr("Context: rip=");
  PrintToStderrPointer(ip);
  PrintToStderr(" rsp=");
  PrintToStderrPointer(sp);
  PrintToStderr(" rbp=");
  PrintToStderrPointer(bp);
  PrintToStderr("\n");
  PrintToStderr("Regs: rax=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rax)));
  PrintToStderr(" rbx=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rbx)));
  PrintToStderr(" rcx=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rcx)));
  PrintToStderr(" rdx=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rdx)));
  PrintToStderr("\n");
  PrintToStderr("Regs: rdi=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rdi)));
  PrintToStderr(" rsi=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.rsi)));
  PrintToStderr(" r12=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.r12)));
  PrintToStderr(" r13=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.r13)));
  PrintToStderr(" r14=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.r14)));
  PrintToStderr(" r15=");
  PrintToStderrPointer(reinterpret_cast<const void*>(
      static_cast<uintptr_t>(context->uc_mcontext.cpu.r15)));
  PrintToStderr("\n");

  PrintToStderr("Symbol: ");
  PrintSymbolForAddress(ip);
  PrintToStderr("\n");
  PrintFramePointerBacktrace(sp, bp);
#endif
}

void StackDumpSignalHandler(int signal, siginfo_t* info, void* void_context) {
  // NOTE: This function should stay as close to async-signal safe as possible.

  // Give a first-chance callback (e.g., V8 Wasm guard-region handler) an
  // opportunity to recover before we print the crash.
  if (try_handle_signal != nullptr &&
      try_handle_signal(signal, info, void_context)) {
    // Re-install ourselves — SA_RESETHAND removed us on entry.
    struct sigaction action;
    UNSAFE_TODO(memset(&action, 0, sizeof(action)));
    action.sa_flags = static_cast<int>(SA_RESETHAND | SA_SIGINFO);
    action.sa_sigaction = &StackDumpSignalHandler;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
    return;
  }

  in_signal_handler = 1;

  if (BeingDebugged()) {
    BreakDebugger();
  }

  PrintToStderr("Received signal ");
  char sigbuf[8] = {'\0'};
  internal::itoa_r(signal, 10, 0, sigbuf);
  PrintToStderr(sigbuf);
  if (signal == SIGSEGV && info) {
    PrintToStderr(" si_addr=");
    char addr[19] = {'\0'};
    addr[0] = '0';
    addr[1] = 'x';
    internal::itoa_r(reinterpret_cast<intptr_t>(info->si_addr), 16, 0,
                     base::span<char>(addr + 2, sizeof(addr) - 2));
    PrintToStderr(addr);
  }
  PrintToStderr("\n");
  PrintCrashContext(void_context);

  // Print raw stack trace (async-signal safe — addresses only).
  PrintToStderr("==== C stack trace ====\n");
  StackTrace stack_trace;
  stack_trace.Print();

  PrintToStderr("\n");

  // Reset the default handler and re-raise so the OS can produce a core dump.
  ::signal(signal, SIG_DFL);
  raise(signal);
}

}  // namespace

// ── CollectStackTrace (async-signal safe path) ────────────────────────────────

size_t CollectStackTrace(span<const void*> trace) {
  if (trace.empty()) {
    return 0;
  }

  // On QNX x86_64, bt_get_backtrace() in libbacktrace.so returns 0 frames
  // because the binary's unwind information isn't in a format the library
  // expects. Fall back to frame-pointer-based stack walking, which works
  // because Chromium builds QNX with -fno-omit-frame-pointer.
  //
  // x86_64 frame layout:
  //   [rbp+0]  = saved rbp (previous frame pointer)
  //   [rbp+8]  = return address

  size_t count = 0;
#if defined(__x86_64__) || defined(__X86_64__)
  // Get the current frame pointer using __builtin_frame_address.
  // We use NO_SANITIZE("address") to avoid ASan's fake frame pointers.
  uintptr_t rbp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  const uintptr_t stack_start = reinterpret_cast<uintptr_t>(
      __builtin_frame_address(0));
  constexpr uintptr_t kMaxStackSize = 64u * 1024u * 1024u;  // 64 MB
  const uintptr_t stack_low =
      (stack_start > kMaxStackSize) ? stack_start - kMaxStackSize : 0u;

  for (size_t i = 0; i < StackTrace::kMaxTraces && count < trace.size(); ++i) {
    if (rbp < stack_low || rbp > stack_start + kMaxStackSize) {
      // Frame pointer out of plausible range — stop walking.
      break;
    }

    const uintptr_t* fp_words =
        reinterpret_cast<const uintptr_t*>(rbp);
    const uintptr_t next_rbp = fp_words[0];
    const uintptr_t return_addr = fp_words[1];

    if (return_addr == 0) {
      break;
    }

    trace[count] = reinterpret_cast<const void*>(return_addr);
    ++count;

    if (next_rbp <= rbp) {
      // Frame pointer didn't advance (or went backwards) — stop.
      break;
    }
    rbp = next_rbp;
  }
#else
  // Non-x86_64: fall back to bt_get_backtrace().
  bt_addr_t addrs[StackTrace::kMaxTraces] = {};
  const int max = base::saturated_cast<int>(
      std::min(trace.size(), StackTrace::kMaxTraces));
  int n = bt_get_backtrace(&GetBtAccSelf(), addrs, max);
  if (n > 0) {
    count = std::min(base::saturated_cast<size_t>(n), trace.size());
    for (size_t i = 0; i < count; ++i) {
      trace[i] = reinterpret_cast<const void*>(addrs[i]);
    }
  }
#endif
  return count;
}

// ── Print / OutputToStream ────────────────────────────────────────────────────

// static
void StackTrace::PrintMessageWithPrefix(cstring_view prefix_string,
                                        cstring_view message) {
  // async-signal safe.
  if (!prefix_string.empty()) {
    PrintToStderr(prefix_string.c_str());
  }
  PrintToStderr(message.c_str());
}

void StackTrace::PrintWithPrefixImpl(cstring_view prefix_string) const {
  PrintBacktraceOutputHandler handler;
  ProcessBacktrace(addresses(), prefix_string, &handler);
}

void StackTrace::OutputToStreamWithPrefixImpl(
    std::ostream* os,
    cstring_view prefix_string) const {
  StreamBacktraceOutputHandler handler(os);
  ProcessBacktrace(addresses(), prefix_string, &handler);
}

// ── Signal handler installation ───────────────────────────────────────────────

bool EnableInProcessStackDumping() {
  // Warm up the QNX backtrace library before registering signal handlers
  // so the first crash-path invocation doesn't malloc inside the handler.
  bt_addr_t dummy[4] = {};
  bt_get_backtrace(&GetBtAccSelf(), dummy, 4);

  // Ignore SIGPIPE — applications typically expect this.
  struct sigaction sigpipe_action;
  UNSAFE_TODO(memset(&sigpipe_action, 0, sizeof(sigpipe_action)));
  sigpipe_action.sa_handler = SIG_IGN;
  sigemptyset(&sigpipe_action.sa_mask);
  bool success = (sigaction(SIGPIPE, &sigpipe_action, nullptr) == 0);

  struct sigaction action;
  UNSAFE_TODO(memset(&action, 0, sizeof(action)));
  action.sa_flags = static_cast<int>(SA_RESETHAND | SA_SIGINFO);
  action.sa_sigaction = &StackDumpSignalHandler;
  sigemptyset(&action.sa_mask);

  success &= (sigaction(SIGILL,  &action, nullptr) == 0);
  success &= (sigaction(SIGABRT, &action, nullptr) == 0);
  success &= (sigaction(SIGFPE,  &action, nullptr) == 0);
  success &= (sigaction(SIGBUS,  &action, nullptr) == 0);
  success &= (sigaction(SIGSEGV, &action, nullptr) == 0);
  success &= (sigaction(SIGSYS,  &action, nullptr) == 0);

  return success;
}

bool SetStackDumpFirstChanceCallback(bool (*handler)(int, siginfo_t*, void*)) {
  DCHECK(try_handle_signal == nullptr || handler == nullptr);
  try_handle_signal = handler;
  return true;
}

namespace internal {

// itoa_r: signal-safe integer-to-ASCII conversion.
// Copied verbatim from stack_trace_posix.cc (which is excluded from QNX
// builds).  Both files must stay in sync.
void itoa_r(intptr_t i, int base, size_t padding, base::span<char> buf) {
  if (buf.empty()) {
    return;
  }
  if (base < 2 || base > 16) {
    buf[0u] = '\000';
    return;
  }

  auto writer = base::SpanWriter(buf);
  size_t start = 0u;
  uintptr_t j = static_cast<uintptr_t>(i);

  if (i < 0 && base == 10) {
    j = static_cast<uintptr_t>(-(i + 1)) + 1;
    if (!writer.Write('-')) {
      buf[0u] = '\000';
      return;
    }
    start += 1u;
  }

  constexpr std::string_view digits = "0123456789abcdef";
  do {
    if (!writer.Write(digits[j % static_cast<uintptr_t>(base)])) {
      buf[0] = '\000';
      return;
    }
    j /= static_cast<uintptr_t>(base);
    if (padding > 0) {
      padding--;
    }
  } while (j > 0 || padding > 0);

  if (!writer.Write('\000')) {
    buf[0] = '\000';
    return;
  }

  std::ranges::reverse(buf.first(writer.num_written() - 1u).subspan(start));
}

}  // namespace internal

}  // namespace base::debug
