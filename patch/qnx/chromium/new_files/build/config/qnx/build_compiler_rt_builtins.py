#!/usr/bin/env python3

# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess
import sys

X64_SOURCES = [
    "absvdi2.c",
    "absvsi2.c",
    "absvti2.c",
    "adddf3.c",
    "addsf3.c",
    "addtf3.c",
    "addvdi3.c",
    "addvsi3.c",
    "addvti3.c",
    "apple_versioning.c",
    "ashldi3.c",
    "ashlti3.c",
    "ashrdi3.c",
    "ashrti3.c",
    "bswapdi2.c",
    "bswapsi2.c",
    "clear_cache.c",
    "clzdi2.c",
    "clzsi2.c",
    "clzti2.c",
    "cmpdi2.c",
    "cmpti2.c",
    "comparedf2.c",
    "comparesf2.c",
    "comparetf2.c",
    "cpu_model/x86.c",
    "ctzdi2.c",
    "ctzsi2.c",
    "ctzti2.c",
    "divdc3.c",
    "divdf3.c",
    "divdi3.c",
    "divmoddi4.c",
    "divmodsi4.c",
    "divmodti4.c",
    "divsc3.c",
    "divsf3.c",
    "divsi3.c",
    "divtc3.c",
    "divtf3.c",
    "divti3.c",
    "divxc3.c",
    "emutls.c",
    "enable_execute_stack.c",
    "eprintf.c",
    "extendbfsf2.c",
    "extenddftf2.c",
    "extendhfdf2.c",
    "extendhfsf2.c",
    "extendhftf2.c",
    "extendhfxf2.c",
    "extendsfdf2.c",
    "extendsftf2.c",
    "extendxftf2.c",
    "ffsdi2.c",
    "ffssi2.c",
    "ffsti2.c",
    "fixdfdi.c",
    "fixdfsi.c",
    "fixdfti.c",
    "fixsfdi.c",
    "fixsfsi.c",
    "fixsfti.c",
    "fixtfdi.c",
    "fixtfsi.c",
    "fixtfti.c",
    "fixunsdfdi.c",
    "fixunsdfsi.c",
    "fixunsdfti.c",
    "fixunssfdi.c",
    "fixunssfsi.c",
    "fixunssfti.c",
    "fixunstfdi.c",
    "fixunstfsi.c",
    "fixunstfti.c",
    "fixunsxfdi.c",
    "fixunsxfsi.c",
    "fixunsxfti.c",
    "fixxfdi.c",
    "fixxfti.c",
    "floatditf.c",
    "floatsidf.c",
    "floatsisf.c",
    "floatsitf.c",
    "floattidf.c",
    "floattisf.c",
    "floattitf.c",
    "floattixf.c",
    "floatunditf.c",
    "floatunsidf.c",
    "floatunsisf.c",
    "floatunsitf.c",
    "floatuntidf.c",
    "floatuntisf.c",
    "floatuntitf.c",
    "floatuntixf.c",
    "gcc_personality_v0.c",
    "i386/fp_mode.c",
    "int_util.c",
    "lshrdi3.c",
    "lshrti3.c",
    "moddi3.c",
    "modsi3.c",
    "modti3.c",
    "muldc3.c",
    "muldf3.c",
    "muldi3.c",
    "mulodi4.c",
    "mulosi4.c",
    "muloti4.c",
    "mulsc3.c",
    "mulsf3.c",
    "multc3.c",
    "multf3.c",
    "multi3.c",
    "mulvdi3.c",
    "mulvsi3.c",
    "mulvti3.c",
    "mulxc3.c",
    "negdf2.c",
    "negdi2.c",
    "negsf2.c",
    "negti2.c",
    "negvdi2.c",
    "negvsi2.c",
    "negvti2.c",
    "os_version_check.c",
    "paritydi2.c",
    "paritysi2.c",
    "parityti2.c",
    "popcountdi2.c",
    "popcountsi2.c",
    "popcountti2.c",
    "powidf2.c",
    "powisf2.c",
    "powitf2.c",
    "powixf2.c",
    "subdf3.c",
    "subsf3.c",
    "subtf3.c",
    "subvdi3.c",
    "subvsi3.c",
    "subvti3.c",
    "trampoline_setup.c",
    "truncdfbf2.c",
    "truncdfhf2.c",
    "truncdfsf2.c",
    "truncsfbf2.c",
    "truncsfhf2.c",
    "trunctfbf2.c",
    "trunctfdf2.c",
    "trunctfhf2.c",
    "trunctfsf2.c",
    "trunctfxf2.c",
    "truncxfbf2.c",
    "truncxfhf2.c",
    "ucmpdi2.c",
    "ucmpti2.c",
    "udivdi3.c",
    "udivmoddi4.c",
    "udivmodsi4.c",
    "udivmodti4.c",
    "udivsi3.c",
    "udivti3.c",
    "umoddi3.c",
    "umodsi3.c",
    "umodti3.c",
    "x86_64/floatdidf.c",
    "x86_64/floatdisf.c",
    "x86_64/floatdixf.c",
    "x86_64/floatundidf.S",
    "x86_64/floatundisf.S",
    "x86_64/floatundixf.S",
]

ARM64_SOURCES = [
    "aarch64/fp_mode.c",
    "aarch64/lse.S",
    "aarch64/sme-abi-assert.c",
    "aarch64/sme-abi.S",
    "aarch64/sme-libc-opt-memcpy-memmove.S",
    "aarch64/sme-libc-opt-memset-memchr.S",
    "absvdi2.c",
    "absvsi2.c",
    "absvti2.c",
    "adddf3.c",
    "addsf3.c",
    "addtf3.c",
    "addvdi3.c",
    "addvsi3.c",
    "addvti3.c",
    "apple_versioning.c",
    "ashldi3.c",
    "ashlti3.c",
    "ashrdi3.c",
    "ashrti3.c",
    "bswapdi2.c",
    "bswapsi2.c",
    "clear_cache.c",
    "clzdi2.c",
    "clzsi2.c",
    "clzti2.c",
    "cmpdi2.c",
    "cmpti2.c",
    "comparedf2.c",
    "comparesf2.c",
    "comparetf2.c",
    "cpu_model/aarch64.c",
    "ctzdi2.c",
    "ctzsi2.c",
    "ctzti2.c",
    "divdc3.c",
    "divdf3.c",
    "divdi3.c",
    "divmoddi4.c",
    "divmodsi4.c",
    "divmodti4.c",
    "divsc3.c",
    "divsf3.c",
    "divsi3.c",
    "divtc3.c",
    "divtf3.c",
    "divti3.c",
    "emutls.c",
    "enable_execute_stack.c",
    "eprintf.c",
    "extendbfsf2.c",
    "extenddftf2.c",
    "extendhfdf2.c",
    "extendhfsf2.c",
    "extendhftf2.c",
    "extendsfdf2.c",
    "extendsftf2.c",
    "ffsdi2.c",
    "ffssi2.c",
    "ffsti2.c",
    "fixdfdi.c",
    "fixdfsi.c",
    "fixdfti.c",
    "fixsfdi.c",
    "fixsfsi.c",
    "fixsfti.c",
    "fixtfdi.c",
    "fixtfsi.c",
    "fixtfti.c",
    "fixunsdfdi.c",
    "fixunsdfsi.c",
    "fixunsdfti.c",
    "fixunssfdi.c",
    "fixunssfsi.c",
    "fixunssfti.c",
    "fixunstfdi.c",
    "fixunstfsi.c",
    "fixunstfti.c",
    "floatdidf.c",
    "floatdisf.c",
    "floatditf.c",
    "floatsidf.c",
    "floatsisf.c",
    "floatsitf.c",
    "floattidf.c",
    "floattisf.c",
    "floattitf.c",
    "floatundidf.c",
    "floatundisf.c",
    "floatunditf.c",
    "floatunsidf.c",
    "floatunsisf.c",
    "floatunsitf.c",
    "floatuntidf.c",
    "floatuntisf.c",
    "floatuntitf.c",
    "gcc_personality_v0.c",
    "int_util.c",
    "lshrdi3.c",
    "lshrti3.c",
    "moddi3.c",
    "modsi3.c",
    "modti3.c",
    "muldc3.c",
    "muldf3.c",
    "muldi3.c",
    "mulodi4.c",
    "mulosi4.c",
    "muloti4.c",
    "mulsc3.c",
    "mulsf3.c",
    "multc3.c",
    "multf3.c",
    "multi3.c",
    "mulvdi3.c",
    "mulvsi3.c",
    "mulvti3.c",
    "negdf2.c",
    "negdi2.c",
    "negsf2.c",
    "negti2.c",
    "negvdi2.c",
    "negvsi2.c",
    "negvti2.c",
    "os_version_check.c",
    "paritydi2.c",
    "paritysi2.c",
    "parityti2.c",
    "popcountdi2.c",
    "popcountsi2.c",
    "popcountti2.c",
    "powidf2.c",
    "powisf2.c",
    "powitf2.c",
    "subdf3.c",
    "subsf3.c",
    "subtf3.c",
    "subvdi3.c",
    "subvsi3.c",
    "subvti3.c",
    "trampoline_setup.c",
    "truncdfbf2.c",
    "truncdfhf2.c",
    "truncdfsf2.c",
    "truncsfbf2.c",
    "truncsfhf2.c",
    "trunctfbf2.c",
    "trunctfdf2.c",
    "trunctfhf2.c",
    "trunctfsf2.c",
    "truncxfbf2.c",
    "ucmpdi2.c",
    "ucmpti2.c",
    "udivdi3.c",
    "udivmoddi4.c",
    "udivmodsi4.c",
    "udivmodti4.c",
    "udivsi3.c",
    "udivti3.c",
    "umoddi3.c",
    "umodsi3.c",
    "umodti3.c",
]

ARCH_CONFIGS = {
    "x64": {
        "arch_define": "__X86_64__",
        "sources": X64_SOURCES,
    },
    "arm64": {
        "arch_define": "__AARCH64EL__",
        "sources": ARM64_SOURCES,
    },
}


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    parser.add_argument("--ar", required=True)
    parser.add_argument("--ranlib", required=True)
    parser.add_argument("--sysroot", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--arch", choices=sorted(ARCH_CONFIGS), required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--depfile", required=True)
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    return parser.parse_args()


def parse_make_depfile(depfile_path: Path) -> set[Path]:
    content = depfile_path.read_text(encoding="utf-8")
    content = content.replace("\\\n", " ")
    _, rhs = content.split(":", 1)
    deps = set()
    for token in shlex.split(rhs):
        deps.add(Path(token).resolve())
    return deps


def compile_one(clang: str, source_root: Path, obj_root: Path, common_flags: list[str],
                c_flags: list[str], asm_flags: list[str], rel_source: str) -> tuple[Path, set[Path]]:
    source = source_root / rel_source
    obj = obj_root / (rel_source + ".o")
    depfile = obj.with_suffix(obj.suffix + ".d")
    obj.parent.mkdir(parents=True, exist_ok=True)

    flags = asm_flags if source.suffix == ".S" else c_flags
    cmd = [clang, *common_flags, *flags, "-MD", "-MF", str(depfile), "-c", str(source), "-o", str(obj)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"failed to compile {source}\n"
            f"command: {' '.join(shlex.quote(x) for x in cmd)}\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}")
    deps = parse_make_depfile(depfile)
    deps.add(source.resolve())
    return obj, deps


def write_depfile(depfile_path: Path, output: Path, deps: set[Path]):
    depfile_path.parent.mkdir(parents=True, exist_ok=True)
    depfile_path.write_text(
        f"{output}: {' '.join(sorted(str(dep) for dep in deps))}\n",
        encoding="utf-8")


def main() -> int:
    args = parse_args()
    arch_config = ARCH_CONFIGS[args.arch]

    source_root = Path(args.source_root).resolve()
    output = Path(args.output).resolve()
    depfile_path = Path(args.depfile).resolve()
    work_dir = output.parent / ".build"
    obj_root = work_dir / "obj"

    output.parent.mkdir(parents=True, exist_ok=True)
    work_dir.mkdir(parents=True, exist_ok=True)

    common_flags = [
        f"--target={args.target}",
        f"--sysroot={args.sysroot}",
        "-D__QNXNTO__",
        "-D__QNX__",
        "-DQNX_LIBM_BUILTINS",
        "-D__LITTLEENDIAN__",
        "-D__EXT_XOPEN_EX",
        "-D_QNX_SOURCE",
        "-D_POSIX_C_SOURCE=200809L",
        f"-D{arch_config['arch_define']}",
        "-Uisinf",
        "-Uisnan",
        "-O3",
        "-DNDEBUG",
        "-fno-lto",
        "-nostdinc++",
        "-fPIC",
        "-fno-builtin",
        "-fvisibility=hidden",
        "-fomit-frame-pointer",
        "-DCOMPILER_RT_HAS_FLOAT16",
        "-DVISIBILITY_HIDDEN",
    ]
    c_flags = ["-std=gnu11", "-include", "time.h"]
    asm_flags = []

    deps = set()
    deps.add(Path(__file__).resolve())
    objects = []

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = [
                executor.submit(
                    compile_one,
                    args.clang,
                    source_root,
                    obj_root,
                    common_flags,
                    c_flags,
                    asm_flags,
                    rel_source,
                )
                for rel_source in arch_config["sources"]
            ]
            for future in concurrent.futures.as_completed(futures):
                obj, obj_deps = future.result()
                objects.append(obj)
                deps.update(obj_deps)
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1

    objects.sort()
    archive_tmp = output.with_suffix(output.suffix + ".tmp")
    if archive_tmp.exists():
        archive_tmp.unlink()

    subprocess.run([args.ar, "qc", str(archive_tmp), *(str(obj) for obj in objects)], check=True)
    subprocess.run([args.ranlib, str(archive_tmp)], check=True)
    archive_tmp.replace(output)

    write_depfile(depfile_path, output, deps)
    return 0


if __name__ == "__main__":
    sys.exit(main())
