// thrower_mini.cc: throws std::system_error via recursive std::mutex::lock
// Used in G3 mini smoke.
#include <mutex>
#include <cstdio>

int main() {
    std::mutex m;
    m.lock();
    try {
        m.lock();  // should throw std::system_error(EDEADLK) on QNX
        std::fprintf(stderr, "[thrower] ERROR: recursive lock succeeded (unexpected)\n");
    } catch (const std::system_error& e) {
        std::fprintf(stderr, "[thrower] caught: code=%d what=\"%s\"\n",
                     (int)e.code().value(), e.what());
    }
    m.unlock();
    return 0;
}
