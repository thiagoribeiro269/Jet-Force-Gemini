#!/usr/bin/env python3
"""Cross-build the self-contained Windows native D3D11 preview."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
out = ROOT / "build/port-native"
out.mkdir(parents=True, exist_ok=True)
toolchain = ROOT / "build/port-recomp/cross-toolchain/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64"
compiler = toolchain / "bin/x86_64-w64-mingw32-clang++"
exe = out / "jfg_native_preview.exe"
command = [str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN",
           "-static", str(ROOT / "port/native/render.cpp"), "-o", str(exe), "-ld3d11", "-ld3dcompiler", "-ldxgi", "-lole32"]
subprocess.run(command, check=True)
report = {"status": "built", "source": "port/native/render.cpp", "api": "D3D11",
          "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
          "emulator_dependencies": [], "graphics_libraries": ["Windows D3D11", "Windows D3DCompiler", "Windows DXGI"]}
(out / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
