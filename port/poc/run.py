#!/usr/bin/env python3
"""Build and verify the CPU proof on Linux x86-64 or Windows x64."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def command(label, args, logs):
    log = logs / f"{label}.log"
    print(f"{label}: {log}", flush=True)
    with log.open("w", encoding="utf-8") as file:
        result = subprocess.run([str(arg) for arg in args], cwd=ROOT,
                                stdout=file, stderr=subprocess.STDOUT)
    text = log.read_text(errors="replace").splitlines()
    if result.returncode:
        print("\n".join(text[-45:]), flush=True)
        raise SystemExit(result.returncode)
    print("\n".join(text[-8:]), flush=True)


def find_artifact(folder, name):
    for candidate in (folder / name, folder / "Debug" / name, folder / "Release" / name):
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"Missing build artifact {name} under {folder}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-recomp")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--cases", type=int, default=24)
    args = parser.parse_args()
    if args.jobs < 1 or args.cases < 12:
        parser.error("Use at least one build job and 12 differential cases")
    for dependency in ("elftools", "unicorn"):
        if not importlib.util.find_spec(dependency):
            parser.error("Install port/poc/requirements.txt into the active Python environment")
    vendor = ROOT / "port/vendor/N64ModernRuntime/N64Recomp"
    source = ROOT / "port/recompiler"
    if not (vendor / "lib/rabbitizer/include").is_dir():
        parser.error("Initialize port/vendor/N64ModernRuntime recursively with git submodule update")
    if not args.elf.is_file() or not args.rom.is_file():
        parser.error("Provide the local, verified US ROM and matching N64 ELF")
    out = args.out.resolve()
    proof, toolchain, native = out / "proof", out / "jfg-toolchain", out / "native"
    out.mkdir(parents=True, exist_ok=True)
    generator = ["-G", "Ninja"] if shutil.which("ninja") else (["-A", "x64"] if os.name == "nt" else [])
    command("configure-tool", ["cmake", "-S", source, "-B", toolchain, *generator,
                               "-DCMAKE_BUILD_TYPE=Release"], out)
    command("build-tool", ["cmake", "--build", toolchain, "--config", "Release",
                           "--target", "jfg_recomp_tool", "--parallel", args.jobs], out)
    command("prepare", [sys.executable, ROOT / "port/poc/prepare.py", "--elf", args.elf.resolve(),
                         "--rom", args.rom.resolve(), "--out", proof], out)
    # This directory is exclusively generated output owned by this command.
    if (proof / "generated").exists():
        shutil.rmtree(proof / "generated")
    tool = find_artifact(toolchain, "jfg_recomp_tool.exe" if os.name == "nt" else "jfg_recomp_tool")
    command("recompile", [tool, proof / "recomp.toml"], out)
    command("configure-native", ["cmake", "-S", ROOT / "port/poc", "-B", native, *generator,
                                  "-DCMAKE_BUILD_TYPE=Debug", f"-DJFG_PROOF_DIR={proof.as_posix()}"], out)
    command("build-native", ["cmake", "--build", native, "--config", "Debug", "--parallel", args.jobs], out)
    command("smoke", ["ctest", "--test-dir", native, "-C", "Debug", "--output-on-failure"], out)
    library = find_artifact(native, "jfg_poc.dll" if os.name == "nt" else "libjfg_poc.so")
    command("differential", [sys.executable, ROOT / "port/poc/verify.py", "--manifest", proof / "manifest.json",
                             "--library", library, "--report", out / "report.json", "--cases", args.cases], out)
    print(f"Verified CPU proof. Report: {out / 'report.json'}", flush=True)


if __name__ == "__main__":
    main()
