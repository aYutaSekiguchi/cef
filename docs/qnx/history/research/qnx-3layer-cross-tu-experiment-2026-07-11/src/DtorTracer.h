// DtorTracer.h
// -fno-exceptions TU の frame で local 変数の dtor が unwind 時に呼ばれるか
// 検証するための目印クラス。
#pragma once

#include <cstdio>

namespace throw_qnx {

struct DtorTracer {
    const char* tag;
    explicit DtorTracer(const char* t) : tag(t) {
        std::fprintf(stderr, "[ctor] %s\n", tag);
    }
    ~DtorTracer() {
        std::fprintf(stderr, "[dtor] %s\n", tag);
    }
};

}  // namespace throw_qnx
