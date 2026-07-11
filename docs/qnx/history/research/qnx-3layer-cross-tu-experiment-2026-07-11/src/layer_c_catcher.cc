// layer_c_catcher.cc
// Layer C: 試験的に -fexceptions でビルドし、各 Layer B (or A 直) を
// try / catch (const std::system_error&) で受ける。
// catch では .code() / .what() のみアクセス (RTTI 不要)。
#include "test_mutex.h"
#include "layer_signatures.h"

#include <cstdio>
#include <system_error>

namespace throw_qnx {

// 直 A
__attribute__((noinline))
void CCatchDirectA() {
    std::fprintf(stderr, "[C] case-1: CCatchDirectA entered\n");
    try {
        AThrowsEdeadlkOnRecursiveLock();
    } catch (const std::system_error& e) {
        std::fprintf(stderr,
                     "[C] case-1: CAUGHT std::system_error code=%d what=\"%s\"\n",
                     static_cast<int>(e.code().value()),
                     e.what());
        return;
    } catch (...) {
        std::fprintf(stderr, "[C] case-1: CAUGHT ...\n");
        return;
    }
    std::fprintf(stderr, "[C] case-1: UNREACHED (no exception)\n");
}

// 単純 frame 経由
__attribute__((noinline))
void CCatchThroughBSimple() {
    std::fprintf(stderr, "[C] case-2: CCatchThroughBSimple entered\n");
    try {
        BSimple();
    } catch (const std::system_error& e) {
        std::fprintf(stderr,
                     "[C] case-2: CAUGHT std::system_error code=%d what=\"%s\"\n",
                     static_cast<int>(e.code().value()),
                     e.what());
        return;
    } catch (...) {
        std::fprintf(stderr, "[C] case-2: CAUGHT ...\n");
        return;
    }
    std::fprintf(stderr, "[C] case-2: UNREACHED (no exception)\n");
}

// dtor 持ち frame 経由
__attribute__((noinline))
void CCatchThroughBWithDtors() {
    std::fprintf(stderr, "[C] case-3: CCatchThroughBWithDtors entered\n");
    try {
        BWithDtors();
    } catch (const std::system_error& e) {
        std::fprintf(stderr,
                     "[C] case-3: CAUGHT std::system_error code=%d what=\"%s\"\n",
                     static_cast<int>(e.code().value()),
                     e.what());
        return;
    } catch (...) {
        std::fprintf(stderr, "[C] case-3: CAUGHT ...\n");
        return;
    }
    std::fprintf(stderr, "[C] case-3: UNREACHED (no exception)\n");
}

}  // namespace throw_qnx
