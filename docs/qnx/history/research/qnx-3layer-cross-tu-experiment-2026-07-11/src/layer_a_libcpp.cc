// layer_a_libcpp.cc
// Layer A: libc++ の std::mutex::lock() 相当。-fexceptions でビルドされる。
// 既に同スレッドで保持されているミューテックスを lock すると、
// QNX libc++ は std::system_error(EDEADLK) を throw する。
//
// これは ANGLE の MutexOnStd/GlobalMutex (QNX では std::mutex) が
// 再帰呼び出しされたときに起こす abort 経路と等価。
#include "test_mutex.h"
#include "layer_signatures.h"

#include <mutex>

namespace throw_qnx {

// std::mutex::lock() が EDEADLK を投げる経路を再現。
// A は -fexceptions でビルドされているので、throw は C++ 例外機構に
// 乗る。呼び出し元は std::system_error を catch できる。
__attribute__((noinline))
void AThrowsEdeadlkOnRecursiveLock() {
    std::mutex& m = GetTestMutex();
    // main() 側で既に同じスレッドが lock しているので、2 回目の lock() は
    // QNX の std::mutex 実装 (pthread_mutex_lock + 失敗時 throw) によって
    // std::system_error(EDEADLK, generic_category()) を投げる。
    m.lock();
    // ここには来ない。
}

}  // namespace throw_qnx
