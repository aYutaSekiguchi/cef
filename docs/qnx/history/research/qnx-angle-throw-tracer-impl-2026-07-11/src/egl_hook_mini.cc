// egl_hook_mini.cc: minimal eglGetPlatformDisplay interpose for G4 gate
// -fexceptions -fno-rtti -fvisibility=default -fPIC
#include <cstddef>
#include <cstdint>

#include <unistd.h>
#include <dlfcn.h>

extern "C" {

typedef void* EGLDisplay;
typedef int EGLenum;
typedef void* EGLAttrib;

static EGLDisplay (*s_real_eglGetPlatformDisplay)(EGLenum, void*, const EGLAttrib*) = nullptr;

__attribute__((constructor)) static void init_egl() {
    s_real_eglGetPlatformDisplay = (decltype(s_real_eglGetPlatformDisplay))
        dlsym(RTLD_NEXT, "eglGetPlatformDisplay");
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                 const EGLAttrib* attrib_list);

}  // extern "C"

extern "C" EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                           const EGLAttrib* attrib_list) {
    static const char msg[] = "[MINI-EGL] intercepted eglGetPlatformDisplay\n";
    (void)!write(2, msg, sizeof(msg) - 1);
    if (s_real_eglGetPlatformDisplay) {
        return s_real_eglGetPlatformDisplay(platform, native_display, attrib_list);
    }
    return (EGLDisplay)0;
}
