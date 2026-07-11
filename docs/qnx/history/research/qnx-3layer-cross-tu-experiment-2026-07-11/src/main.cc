// main.cc
// driver: 共有 std::mutex を最初に lock し、case 1..3 のいずれかを
// 実行する。 Layer A/B/C はそれぞれ別の cc を #include せず、
// リンク時に解決されるようにシンボル宣言のみ。
#include "test_mutex.h"
#include "DtorTracer.h"
#include "layer_signatures.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>

// 共有 std::mutex 定義。main TU に置く。
namespace throw_qnx {
std::mutex g_test_mutex;
std::mutex& GetTestMutex() { return g_test_mutex; }
}  // namespace throw_qnx

// Layer C の関数宣言
namespace throw_qnx {
void CCatchDirectA();
void CCatchThroughBSimple();
void CCatchThroughBWithDtors();
}  // namespace throw_qnx

int main(int argc, char** argv) {
    int c = (argc > 1) ? std::atoi(argv[1]) : 0;
    std::fprintf(stderr, "[main] === case %d start ===\n", c);

    // 共有 std::mutex を先に lock しておき、Layer A の再帰 lock を誘発する。
    {
        throw_qnx::DtorTracer init_t("main-init-lock");
        throw_qnx::g_test_mutex.lock();
        std::fprintf(stderr, "[main] g_test_mutex locked, this thread holds it\n");
    }

    int rc = 0;
    switch (c) {
        case 1: throw_qnx::CCatchDirectA(); break;
        case 2: throw_qnx::CCatchThroughBSimple(); break;
        case 3: throw_qnx::CCatchThroughBWithDtors(); break;
        default:
            std::fprintf(stderr, "usage: %s <1|2|3>\n", argv[0]);
            rc = 2;
            break;
    }

    // mutex を解放してから終了
    throw_qnx::g_test_mutex.unlock();
    std::fprintf(stderr, "[main] === case %d end ===\n", c);
    return rc;
}
