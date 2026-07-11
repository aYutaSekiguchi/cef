// layer_b_simple.cc
// Layer B variant 1: 単純 frame。-fno-exceptions -fno-rtti でビルド。
// non-trivial な local を持たず、A を呼ぶだけ。
#include "test_mutex.h"
#include "layer_signatures.h"

namespace throw_qnx {

// A を呼ぶだけの単純 frame。noinline によって確実に frame が残る。
// local 変数なし → unwinder が走っても dtor cleanup 対象なし。
__attribute__((noinline))
void BSimple() {
    AThrowsEdeadlkOnRecursiveLock();
    // ここには来ない。
}

}  // namespace throw_qnx
