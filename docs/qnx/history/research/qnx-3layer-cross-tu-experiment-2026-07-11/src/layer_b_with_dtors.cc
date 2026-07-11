// layer_b_with_dtors.cc
// Layer B variant 2: non-trivial local を持つ frame。-fno-exceptions -fno-rtti。
// std::string / std::vector<int> / DtorTracer を local に持ち、A を呼ぶ。
#include "test_mutex.h"
#include "DtorTracer.h"
#include "layer_signatures.h"

#include <string>
#include <vector>

namespace throw_qnx {

__attribute__((noinline))
void BWithDtors() {
    DtorTracer t1("BWithDtors-entry");
    std::string s = "this string has a non-trivial destructor";
    std::vector<int> v(64, 0x5A);
    DtorTracer t2("BWithDtors-before-call");
    (void)s.size();
    (void)v.size();
    AThrowsEdeadlkOnRecursiveLock();
    DtorTracer t3("BWithDtors-after-call");  // unreachable
}

}  // namespace throw_qnx
