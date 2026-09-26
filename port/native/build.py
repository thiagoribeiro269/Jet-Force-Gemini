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
           "-static", str(ROOT / "port/native/render.cpp"), str(ROOT / "port/native/renderer_d3d11.cpp"),
           "-o", str(exe), "-ld3d11", "-ld3dcompiler", "-ldxgi", "-lole32"]
subprocess.run(command, check=True)
math_exe = out / "check_animation.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_animation.cpp"), "-o", str(math_exe)], check=True)
player_exe = out / "check_player.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_player.cpp"), "-o", str(player_exe)], check=True)
report = {"status": "built", "source": "port/native/render.cpp", "api": "D3D11",
          "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
          "animation_math_executable_sha256": hashlib.sha256(math_exe.read_bytes()).hexdigest(),
          "animation_player_executable_sha256": hashlib.sha256(player_exe.read_bytes()).hexdigest(),
          "emulator_dependencies": [], "graphics_libraries": ["Windows D3D11", "Windows D3DCompiler", "Windows DXGI"]}
character_exe = out / "check_character.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_character.cpp"), "-o", str(character_exe)], check=True)
report["character_executable_sha256"] = hashlib.sha256(character_exe.read_bytes()).hexdigest()
juno_exe = out / "check_juno_selection.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_juno_selection.cpp"), "-o", str(juno_exe)], check=True)
report["juno_selection_executable_sha256"] = hashlib.sha256(juno_exe.read_bytes()).hexdigest()
session_exe = out / "jfg_native_session.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-static",
                str(ROOT / "port/native/session_main.cpp"), str(ROOT / "port/native/renderer_d3d11.cpp"), "-o", str(session_exe),
                "-ld3d11", "-ld3dcompiler", "-ldxgi", "-lole32"], check=True)
session_check = out / "check_session.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_session.cpp"), "-o", str(session_check)], check=True)
report["renderer_sources"] = ["port/native/render.cpp", "port/native/renderer_d3d11.cpp"]
report["native_session_executable_sha256"] = hashlib.sha256(session_exe.read_bytes()).hexdigest()
report["native_session_check_sha256"] = hashlib.sha256(session_check.read_bytes()).hexdigest()
region_exe = out / "jfg_native_region.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN", "-static",
                str(ROOT / "port/native/region_main.cpp"), str(ROOT / "port/native/renderer_d3d11.cpp"), "-o", str(region_exe),
                "-ld3d11", "-ld3dcompiler", "-ldxgi", "-lole32"], check=True)
region_check = out / "check_region.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_region.cpp"), "-o", str(region_check)], check=True)
report["native_region_executable_sha256"] = hashlib.sha256(region_exe.read_bytes()).hexdigest()
report["native_region_check_sha256"] = hashlib.sha256(region_check.read_bytes()).hexdigest()
movement_exe = out / "jfg_native_movement.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN",
                "-static", str(ROOT / "port/native/movement_main.cpp"), str(ROOT / "port/native/renderer_d3d11.cpp"), "-o", str(movement_exe),
                "-ld3d11", "-ld3dcompiler", "-ldxgi", "-lole32"], check=True)
movement_check = out / "check_movement.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_movement.cpp"), "-o", str(movement_check)], check=True)
report["native_movement_executable_sha256"] = hashlib.sha256(movement_exe.read_bytes()).hexdigest()
report["native_movement_check_sha256"] = hashlib.sha256(movement_check.read_bytes()).hexdigest()
play_exe = out / "jfg_native_play.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN",
                "-static", "-mwindows", str(ROOT / "port/native/play_main.cpp"), str(ROOT / "port/native/renderer_d3d11.cpp"),
                "-o", str(play_exe), "-ld3d11", "-ld3dcompiler", "-ldxgi", "-lole32", "-luser32", "-lxinput1_4"], check=True)
replay_exe = out / "jfg_native_replay.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/replay_main.cpp"), "-o", str(replay_exe)], check=True)
play_check = out / "check_play.exe"
subprocess.run([str(compiler), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-static",
                str(ROOT / "port/native/check_play.cpp"), "-o", str(play_check)], check=True)
report["native_play_executable_sha256"] = hashlib.sha256(play_exe.read_bytes()).hexdigest()
report["native_replay_executable_sha256"] = hashlib.sha256(replay_exe.read_bytes()).hexdigest()
report["native_play_check_sha256"] = hashlib.sha256(play_check.read_bytes()).hexdigest()
(out / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
