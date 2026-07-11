// test_mutex.h
// 3 層試験全体で共有する std::mutex を持つグローバル変数。
// main() が init で lock しておき、A の呼び出しで EDEADLK を誘発する。
#pragma once

#include <mutex>

namespace throw_qnx {

// 共有ミューテックス (EDEADLK を発生させるために main 側で先に lock する)。
extern std::mutex& GetTestMutex();

}  // namespace throw_qnx
