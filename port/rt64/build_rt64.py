#!/usr/bin/env python3
"""Build pinned RT64 as a Windows x64 static library from a Linux host.

Build fixes live in a generated source overlay. The pinned RT64 checkout and
the repository's existing vendor trees remain unchanged.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "build/port-graphics/rt64-source"
OUTPUT = ROOT / "build/port-rt64"
TOOLCHAIN = ROOT / "build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64"
CMAKE_TOOLCHAIN = ROOT / "port/poc/windows-llvm-mingw.cmake"
PIN = "43373749dac9bbc1b653e6a02aed40a9e1783bed"


def run(*args: object, cwd: Path | None = None) -> None:
    print("+", " ".join(str(arg) for arg in args), flush=True)
    subprocess.run([str(arg) for arg in args], cwd=cwd, check=True)


def replace_once(content: str, old: str, new: str) -> str:
    if content.count(old) != 1:
        raise RuntimeError(f"RT64 CMake input changed; expected one occurrence of: {old!r}")
    return content.replace(old, new, 1)


def write_if_changed(path: Path, content: str) -> None:
    if path.exists() and path.read_text() == content:
        return
    path.write_text(content)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--rt64-only", action="store_true", help="Build only the RT64 archive")
    args = parser.parse_args()

    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SOURCE, text=True).strip()
    if head != PIN:
        raise RuntimeError(f"Expected RT64 {PIN}, found {head}")
    run("git", "submodule", "update", "--init", "--recursive", "--depth", "1", cwd=SOURCE)
    missing = [path for path in (
        "ddspp", "dxc", "hlslpp", "im3d", "imgui", "implot", "mupen64plus-core",
        "mupen64plus-win32-deps", "nativefiledialog-extended", "plume", "re-spirv",
        "spirv-cross", "stb", "xxHash", "zstd",
    ) if not (SOURCE / "src/contrib" / path / ".git").exists()]
    if missing:
        raise RuntimeError("Initialize pinned RT64 submodules first: " + ", ".join(missing))

    host_dir = OUTPUT / "host-tools"
    host_dir.mkdir(parents=True, exist_ok=True)
    host_tool = host_dir / "file_to_c"
    host_source = SOURCE / "src/tools/file_to_c/file_to_c.cpp"
    if not host_tool.exists() or host_tool.stat().st_mtime < host_source.stat().st_mtime:
        run(shutil.which("c++") or "c++", "-std=c++17", "-O2", host_source, "-o", host_tool)

    staged = OUTPUT / "source"
    staged.mkdir(parents=True, exist_ok=True)
    compat = OUTPUT / "compat-include"
    compat.mkdir(exist_ok=True)
    write_if_changed(compat / "Windows.h", "#pragma once\n#include <windows.h>\n")
    write_if_changed(compat / "Shlobj.h", "#pragma once\n#include <shlobj.h>\n")
    write_if_changed(compat / "ShellScalingAPI.h", "#pragma once\n#include <shellscalingapi.h>\n")
    # This LLVM-MinGW SDK predates GPU upload heaps. Pinned D3D12MA at
    # plume/contrib/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp declares
    # D3D12_HEAP_TYPE_GPU_UPLOAD_COPY = (D3D12_HEAP_TYPE)5 (line 188).
    write_if_changed(compat / "d3d12.h",
        "#pragma once\n#include_next <d3d12.h>\n"
        "#define D3D12_HEAP_TYPE_GPU_UPLOAD ((D3D12_HEAP_TYPE)5)\n"
    )
    # MinGW declares DirectX interface GUIDs in its d3d12/dxgi headers.
    write_if_changed(compat / "dxguids.h", "#pragma once\n#include <d3d12.h>\n#include <dxgi1_6.h>\n")
    # Pinned dxcapi.h uses MSVC uuid attributes, which clang GNU ignores.
    # These are its three exact interface UUIDs used by rt64_shader_compiler.
    write_if_changed(compat / "dxc_uuid.h",
        "#pragma once\n#include <windows.h>\n#include <dxcapi.h>\n"
        "__CRT_UUID_DECL(IDxcCompiler,0x8c210bf3,0x011f,0x4422,0x8d,0x70,0x6f,0x9a,0xcb,0x8d,0xb6,0x17)\n"
        "__CRT_UUID_DECL(IDxcUtils,0x4605c4cb,0x2019,0x492a,0xad,0xa4,0x65,0xf2,0x0b,0xb7,0xd6,0x7f)\n"
        "__CRT_UUID_DECL(IDxcLinker,0xf1b5be2a,0x62dd,0x4327,0xa1,0xc2,0x42,0xac,0x1e,0x1e,0x78,0xe6)\n")
    include_link = staged / "include"
    if not include_link.is_symlink():
        if include_link.exists():
            raise RuntimeError(f"Unexpected source overlay entry: {include_link}")
        include_link.symlink_to(SOURCE / "include", target_is_directory=True)
    src_overlay = staged / "src"
    if src_overlay.is_symlink():
        src_overlay.unlink()
    src_overlay.mkdir(exist_ok=True)
    for child in (SOURCE / "src").iterdir():
        if child.name in ("common", "contrib"):
            continue
        link = src_overlay / child.name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(child, target_is_directory=child.is_dir())
    common_overlay = src_overlay / "common"
    common_overlay.mkdir(exist_ok=True)
    header_name = "rt64_tmem_hasher.h"
    for child in (SOURCE / "src/common").iterdir():
        if child.name == header_name:
            continue
        link = common_overlay / child.name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(child, target_is_directory=child.is_dir())
    # RT64 checks __LP64__ for its builtin; Clang MinGW is LLP64.
    pristine = subprocess.check_output(
        ["git", "show", f"HEAD:src/common/{header_name}"], cwd=SOURCE, text=True)
    patched = replace_once(pristine,
        "#elif defined(__GNUC__) && (__GNUC__ >= 4) && defined(__LP64__)",
        "#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__LP64__) || defined(_WIN64))")
    write_if_changed(common_overlay / header_name, patched)

    # Plume's D3D12 texture-to-buffer readback uses a placed footprint as the
    # destination. That location has no texture, so sample positions must be
    # reset instead of dereferencing dstLocation.texture. Overlay only this TU.
    contrib_overlay = src_overlay / "contrib"
    if contrib_overlay.is_symlink():
        contrib_overlay.unlink()
    contrib_overlay.mkdir(exist_ok=True)
    for child in (SOURCE / "src/contrib").iterdir():
        if child.name == "plume":
            continue
        link = contrib_overlay / child.name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(child, target_is_directory=child.is_dir())
    plume_overlay = contrib_overlay / "plume"
    if plume_overlay.is_symlink():
        plume_overlay.unlink()
    plume_overlay.mkdir(exist_ok=True)
    plume_name = "plume_d3d12.cpp"
    for child in (SOURCE / "src/contrib/plume").iterdir():
        if child.name == plume_name:
            continue
        link = plume_overlay / child.name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(child, target_is_directory=child.is_dir())
    plume_source = SOURCE / "src/contrib/plume" / plume_name
    plume_bytes = plume_source.read_bytes()
    plume_sha256 = "795f1e7d75c450106b7154f4089ea4549fff111e13d6a078b81114c3e0dea317"
    if hashlib.sha256(plume_bytes).hexdigest() != plume_sha256:
        raise RuntimeError(f"Pinned Plume source differs from reviewed SHA-256: {plume_source}")
    plume_patched = replace_once(plume_bytes.decode(),
        "        setSamplePositions(dstLocation.texture);\n"
        "        d3d->CopyTextureRegion(&copyDstLocation, dstX, dstY, dstZ, &copySrcLocation, (srcBox != nullptr) ? &copyBox : nullptr);",
        "        if (dstLocation.texture != nullptr) {\n"
        "            setSamplePositions(dstLocation.texture);\n"
        "        }\n"
        "        else {\n"
        "            resetSamplePositions();\n"
        "        }\n"
        "        d3d->CopyTextureRegion(&copyDstLocation, dstX, dstY, dstZ, &copySrcLocation, (srcBox != nullptr) ? &copyBox : nullptr);")
    write_if_changed(plume_overlay / plume_name, plume_patched)

    cmake = (SOURCE / "CMakeLists.txt").read_text()
    cmake = replace_once(cmake, "add_subdirectory(src/tools/file_to_c)",
        'add_executable(file_to_c IMPORTED GLOBAL)\n'
        f'set_target_properties(file_to_c PROPERTIES IMPORTED_LOCATION "{host_tool}")')
    # Pending for specialized DXIL shaders: dxc-bin@cc15e715 bundles Linux
    # DXC modules with version string 1.8.0.4461 (11e1318c3) and Windows
    # dxcompiler.dll 1.7.0.4147. Even the official Windows v1.8.2403.2 DLL
    # from commit 11e1318c3 emits 1.8.2403.37, so DXC refuses to link those
    # libraries. The controlled harness disables specialization and uses
    # RT64's precompiled ubershaders; do not rewrite DXIL metadata or bypass
    # version validation to make these libraries link.
    cmake = replace_once(cmake,
        "        D3D12\n        DXGI\n        Shcore.lib\n",
        "        d3d12\n        dxgi\n        shcore\n")
    cmake = replace_once(cmake, "endif()\n\nset(ZSTD_LEGACY_SUPPORT OFF)",
        'endif()\n\n# Shader compiler must run on the Linux build host.\n'
        'if (CMAKE_CROSSCOMPILING AND WIN32)\n'
        '    set(DXC "${CMAKE_COMMAND}" "-E" "env"\n'
        '        "LD_LIBRARY_PATH=${PROJECT_SOURCE_DIR}/src/contrib/dxc/lib/x64"\n'
        '        "${PROJECT_SOURCE_DIR}/src/contrib/dxc/bin/x64/dxc-linux")\n'
        f'endif()\n\ninclude_directories(BEFORE "{compat}")\n'
        'add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)\n\n'
        'set(ZSTD_LEGACY_SUPPORT OFF)')
    # The pinned TU uses std::acos without including <cmath>; force it only there.
    cmake += ('\nset_source_files_properties("${PROJECT_SOURCE_DIR}/src/hle/rt64_rigid_body.cpp" '
              'PROPERTIES COMPILE_OPTIONS "-include;cmath")\n')
    # The pinned source spells the Windows export modifier as _declspec.
    cmake += ('set_source_files_properties("${PROJECT_SOURCE_DIR}/src/render/rt64_optimus.cpp" '
              'PROPERTIES COMPILE_DEFINITIONS "_declspec=__declspec")\n')
    cmake += (f'set_source_files_properties("${{PROJECT_SOURCE_DIR}}/src/render/rt64_shader_compiler.cpp" '
              f'PROPERTIES COMPILE_OPTIONS "-include;{compat}/dxc_uuid.h")\n')
    harness = ROOT / "port/rt64/headless.cpp"
    adapter = ROOT / "port/rt64/adapter.cpp"
    if harness.exists() and adapter.exists():
        cmake += ("\n# JFG Windows console proof uses the same RT64/dependency targets.\n"
                  f'add_executable(jfg_rt64_headless "{harness}" "{adapter}")\n'
                  "target_compile_definitions(jfg_rt64_headless PRIVATE RT_ENABLED=0 SCRIPT_ENABLED=0)\n"
                  "target_link_libraries(jfg_rt64_headless PRIVATE rt64 shell32 ole32 user32 dwmapi d3dcompiler)\n"
                  "target_link_options(jfg_rt64_headless PRIVATE -static-libstdc++ -static-libgcc)\n")
    decoder_test = ROOT / "port/rt64/decoder_test.cpp"
    if adapter.exists() and decoder_test.exists():
        cmake += ("\n# Decoder-only Windows regression binary needs no GPU or RT64 linkage.\n"
                  f'add_executable(jfg_rt64_decoder_test "{decoder_test}" "{adapter}")\n'
                  "target_compile_definitions(jfg_rt64_decoder_test PRIVATE JFG_ADAPTER_DECODE_ONLY=1)\n"
                  "target_link_options(jfg_rt64_decoder_test PRIVATE -static-libstdc++ -static-libgcc)\n")
    write_if_changed(staged / "CMakeLists.txt", cmake)

    build = OUTPUT / "windows-x64"
    run("cmake", "-S", staged, "-B", build, "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={CMAKE_TOOLCHAIN}",
        f"-DJFG_LLVM_MINGW_ROOT={TOOLCHAIN}",
        "-DRT64_STATIC=ON", "-DCMAKE_BUILD_TYPE=Release")
    if not args.configure_only:
        target = "rt64" if args.rt64_only or not (harness.exists() and adapter.exists()) else "jfg_rt64_headless"
        targets = [target]
        if target != "rt64" and decoder_test.exists():
            targets.append("jfg_rt64_decoder_test")
        run("cmake", "--build", build, "--target", *targets, "--parallel", "2")
        if target == "jfg_rt64_headless":
            sdl = SOURCE / "src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/lib/x64/SDL2.dll"
            shutil.copy2(sdl, build / "SDL2.dll")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"RT64 build failed: {exc}", file=sys.stderr)
        sys.exit(1)
