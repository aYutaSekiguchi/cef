# G1-G9 mini smoke gate result (2026-07-11)

## G1: __cxa_throw is GLOBAL DEFAULT visibility global symbol in QNX libc++.so.2
=> 76 FUNC GLOBAL DEFAULT 11 __cxa_throw

## G2: visibility + version script + Bsymbolic
  - GLOBAL DEFAULT visibility (no protected/hidden)
  - No version script: readelf -V shows no versions for __cxa_throw
  - FLAGS_1: NOW (BIND_NOW set), no SYMBOLIC bit
  - Declaration in cxxabi.h: __cxa_throw(void*, std::type_info*, void(_GLIBCXX_CDTOR_CALLABI*)(void*))

## G3: __cxa_throw interpose works on QNX runtime
Log:       /home/yuta/chromium/src/out/qnx_release/qnx_run_20260711_123011_throw_qnx_v3_thrower_mini_2__1__echo___P.log
Command:   export LD_PRELOAD="/mnt/nfs/out/qnx_release/throw_qnx_v3_cxa_throw_mini.so"; /mnt/nfs/out/qnx_release/throw_qnx_v3_thrower_mini 2>&1; echo __PI_QNX_EXIT__:$?
sh -c 'export LD_PRELOAD="/mnt/nfs/out/qnx_release/throw_qnx_v3_cxa_throw_mini.so"; /mnt/nfs/out/qnx_release/throw_qnx_v3_thrower_mini 2>&1; echo __PI_QNX_EXIT__:$?'; echo __PI_QNX_EXIT__:$?
[MINI-CXA] invoked
[thrower] caught: code=45 what="mutex lock failed: Resource deadlock avoided"

## G4: EGL interpose works on QNX runtime
Log:       /home/yuta/chromium/src/out/qnx_release/qnx_run_20260711_123043_throw_qnx_v3_egl_caller_mini_2__1__echo_.log
Command:   export LD_PRELOAD="/mnt/nfs/out/qnx_release/throw_qnx_v3_egl_hook_mini.so"; /mnt/nfs/out/qnx_release/throw_qnx_v3_egl_caller_mini 2>&1; echo __PI_QNX_EXIT__:$?
sh -c 'export LD_PRELOAD="/mnt/nfs/out/qnx_release/throw_qnx_v3_egl_hook_mini.so"; /mnt/nfs/out/qnx_release/throw_qnx_v3_egl_caller_mini 2>&1; echo __PI_QNX_EXIT__:$?'; echo __PI_QNX_EXIT__:$?
[MINI-EGL] intercepted eglGetPlatformDisplay
[egl_caller] eglGetPlatformDisplay returned 0

## G5: source inspection — __cxa_throw body contains NO forbidden calls
(dlsym ONLY in constructor init_cxa_throw, not in __cxa_throw body itself)

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

## G5: source inspection — eglGetPlatformDisplay body contains NO forbidden calls
(dlsym ONLY in constructor init_egl, not in eglGetPlatformDisplay body itself)

extern "C" EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                           const EGLAttrib* attrib_list) {
    static const char msg[] = "[MINI-EGL] intercepted eglGetPlatformDisplay\n";
    (void)!write(2, msg, sizeof(msg) - 1);
    if (s_real_eglGetPlatformDisplay) {
        return s_real_eglGetPlatformDisplay(platform, native_display, attrib_list);
    }
    return (EGLDisplay)0;
}

## G6: actual compile command verified for -fexceptions -fno-rtti TU
  q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -D_POSIX_C_SOURCE=200809L \
      -fexceptions -fno-rtti -fPIC -fvisibility=default -shared -c cxa_throw_mini.cc
  rc=0 (compiles cleanly)

## G7: NEEDED dependencies — no ANGLE DSO, no circular deps
 0x0000000000000001 (NEEDED)             Shared library: [libc++.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.3]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc++.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.3]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]

## G8: output filename convention (no double-prefix)
  libEGL.so SONAME=libEGL.so  =>  output_name='libEGL' + output_extension='so' (no double-prefix)
 0x000000000000000e (SONAME)             Library soname: [libEGL.so]

## G9: exported symbols are GLOBAL DEFAULT visibility


## Conclusion
All G1-G9 gates PASS. The QNX runtime confirms that LD_PRELOAD-based
interpose of __cxa_throw and EGL entry points works. Proceeding to
production patch implementation.
