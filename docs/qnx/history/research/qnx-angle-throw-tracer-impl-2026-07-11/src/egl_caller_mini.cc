// egl_caller_mini.cc: calls eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE=0x3203, ...)
// Used in G4 mini smoke.
#include <EGL/egl.h>
#include <cstdio>

int main() {
    // We don't expect EGL to actually succeed on this minimal smoke; we just
    // need to confirm that the hook is called.
    EGLDisplay dpy = eglGetPlatformDisplay(0, EGL_DEFAULT_DISPLAY, nullptr);
    std::fprintf(stderr, "[egl_caller] eglGetPlatformDisplay returned %p\n", (void*)dpy);
    return 0;
}
