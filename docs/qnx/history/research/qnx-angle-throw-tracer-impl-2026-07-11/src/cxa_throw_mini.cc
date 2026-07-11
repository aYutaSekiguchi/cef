// cxa_throw_mini.cc: minimal __cxa_throw interpose for G3 gate
// -fexceptions -fno-rtti -fPIC
// No allocation, no fprintf, no std::call_once, no lock. raw write(2) only.
#include <cstddef>
#include <cstdint>

#include <unistd.h>
#include <dlfcn.h>

extern "C" {

typedef void (*cxa_dtor_t)(void*);
static void (*s_real_cxa_throw)(void*, void*, cxa_dtor_t) = nullptr;

__attribute__((constructor)) static void init_cxa_throw() {
    s_real_cxa_throw = (decltype(s_real_cxa_throw))dlsym(RTLD_NEXT, "__cxa_throw");
}

void __cxa_throw(void* thrown_object, void* tinfo, cxa_dtor_t destructor);

static thread_local int tls_in_hook_depth = 0;

}  // extern "C"

extern "C" void __cxa_throw(void* thrown_object, void* tinfo, cxa_dtor_t destructor) {
    if (tls_in_hook_depth > 0) {
        if (s_real_cxa_throw) s_real_cxa_throw(thrown_object, tinfo, destructor);
        __builtin_unreachable();
    }
    ++tls_in_hook_depth;
    static const char msg[] = "[MINI-CXA] invoked\n";
    (void)!write(2, msg, sizeof(msg) - 1);
    --tls_in_hook_depth;
    if (s_real_cxa_throw) s_real_cxa_throw(thrown_object, tinfo, destructor);
    __builtin_unreachable();
}
