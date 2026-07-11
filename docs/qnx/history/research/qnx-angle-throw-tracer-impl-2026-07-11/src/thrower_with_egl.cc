// Combined: call eglGetPlatformDisplay (pushes tls_egl_call_depth via hook)
// then trigger std::system_error via recursive mutex
#include <EGL/egl.h>
#include <mutex>
#include <cstdio>

int main() {
    EGLDisplay dpy = eglGetPlatformDisplay(EGLenum(0), EGL_DEFAULT_DISPLAY, nullptr);
    std::fprintf(stderr, "[thrower_with_egl] eglGetPlatformDisplay returned %p\n", (void*)dpy);
    std::mutex m;
    m.lock();
    try {
        m.lock();  // throws std::system_error
    } catch (const std::system_error& e) {
        std::fprintf(stderr, "[thrower_with_egl] caught: code=%d what=\"%s\"\n",
                     (int)e.code().value(), e.what());
    }
    m.unlock();
    return 0;
}
