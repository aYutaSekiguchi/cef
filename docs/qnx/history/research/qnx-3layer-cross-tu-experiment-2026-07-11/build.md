=== /tmp/throw_qnx_v2 build log (cross-TU experiment, 2026-07-11) ===
Toolchain: q++ -Vgcc_ntox86_64 (QNX 8.0 SDP / x86_64-pc-nto-qnx8.0.0-gcc 12.2.0)
Target  : x86_64-unknown-nto
Sysroot : /home/yuta/qnx800/target/qnx
Standard: -std=c++17 -O0 -g

--- per-TU flags ---
layer_a_libcpp.cc       (libc++ mutex 模擬, throws via std::mutex::lock()): -fexceptions -fno-rtti
layer_b_simple.cc       (-fno-exceptions 中間 frame, simple):           -fno-exceptions -fno-rtti
layer_b_with_dtors.cc   (-fno-exceptions 中間 frame, non-trivial dtor): -fno-exceptions -fno-rtti
layer_c_catcher.cc      (-fexceptions wrapper, try/catch):                -fexceptions -fno-rtti
main.cc                 (-fexceptions driver):                            -fexceptions -fno-rtti

--- compile invocations ---
q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -I/tmp/throw_qnx_v2 -fexceptions -fno-rtti -c layer_a_libcpp.cc -o /tmp/throw_qnx_v2/build/layer_a.o
q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -I/tmp/throw_qnx_v2 -fno-exceptions -fno-rtti -c layer_b_simple.cc -o /tmp/throw_qnx_v2/build/layer_b_simple.o
q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -I/tmp/throw_qnx_v2 -fno-exceptions -fno-rtti -c layer_b_with_dtors.cc -o /tmp/throw_qnx_v2/build/layer_b_dtors.o
q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -I/tmp/throw_qnx_v2 -fexceptions -fno-rtti -c layer_c_catcher.cc -o /tmp/throw_qnx_v2/build/layer_c.o
q++ -Vgcc_ntox86_64 -std=c++17 -O0 -g -I/tmp/throw_qnx_v2 -fexceptions -fno-rtti -c main.cc -o /tmp/throw_qnx_v2/build/main.o

--- link ---
q++ -Vgcc_ntox86_64 -o /tmp/throw_qnx_v2/build/throw_qnx \
    /tmp/throw_qnx_v2/build/main.o \
    /tmp/throw_qnx_v2/build/layer_a.o \
    /tmp/throw_qnx_v2/build/layer_b_simple.o \
    /tmp/throw_qnx_v2/build/layer_b_dtors.o \
    /tmp/throw_qnx_v2/build/layer_c.o

--- linked binary NEEDED ---
 0x0000000000000001 (NEEDED)             Shared library: [libc++.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libm.so.3]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libgcc_s.so.1]

--- per-TU personality refs (B は -fno-exceptions なので personality 参照なしのはず) ---
=== main.o ===
    29: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND __gxx_personality_v0
=== layer_a.o ===
=== layer_b_simple.o ===
=== layer_b_dtors.o ===
=== layer_c.o ===
    25: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND __cxa_begin_catch
    29: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND __gxx_personality_v0

--- linked binary personality symbols ---
    19: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __gxx_personality_v0
    26: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __cxa_begin_catch
   134: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __gxx_personality_v0
   185: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND __cxa_begin_catch
