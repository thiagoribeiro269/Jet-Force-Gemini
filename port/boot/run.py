#!/usr/bin/env python3
"""Build and verify the original game's native heap/linker startup slice."""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("poc_runner", ROOT / "port/poc/run.py")
poc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(poc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-boot")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("Use at least one build job")
    for dependency in ("elftools", "unicorn"):
        if not importlib.util.find_spec(dependency):
            parser.error("Install port/poc/requirements.txt into the active Python environment")
    if not args.elf.is_file() or not args.rom.is_file():
        parser.error("Provide the verified US ROM and matching N64 ELF")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    proof, toolchain, native = out / "proof", out / "jfg-toolchain", out / "native"
    generator = ["-G", "Ninja"] if shutil.which("ninja") else (["-A", "x64"] if os.name == "nt" else [])

    def command(label, arguments):
        poc.command(label, arguments, out)

    command("configure-tool", ["cmake", "-S", ROOT / "port/recompiler", "-B", toolchain, *generator,
                               "-DCMAKE_BUILD_TYPE=Release"])
    command("build-tool", ["cmake", "--build", toolchain, "--config", "Release",
                           "--target", "jfg_recomp_tool", "--parallel", args.jobs])
    command("prepare", [sys.executable, ROOT / "port/boot/prepare_boot.py", "--elf", args.elf.resolve(),
                         "--rom", args.rom.resolve(), "--out", proof])
    # This is exclusively the output generated for this profile.
    if (proof / "generated").exists():
        shutil.rmtree(proof / "generated")
    tool = poc.find_artifact(toolchain, "jfg_recomp_tool.exe" if os.name == "nt" else "jfg_recomp_tool")
    command("recompile", [tool, proof / "recomp.toml"])
    command("configure-native", ["cmake", "-S", ROOT / "port/poc", "-B", native, *generator,
                                  "-DCMAKE_BUILD_TYPE=Debug", "-DJFG_BOOT_PROFILE=ON",
                                  f"-DJFG_PROOF_DIR={proof.as_posix()}"])
    command("build-native", ["cmake", "--build", native, "--config", "Debug", "--parallel", args.jobs])
    command("smoke", ["ctest", "--test-dir", native, "-C", "Debug", "--output-on-failure"])
    library = poc.find_artifact(native, "jfg_poc.dll" if os.name == "nt" else "libjfg_poc.so")
    command("differential", [sys.executable, ROOT / "port/boot/verify_boot.py", "--manifest", proof / "manifest.json",
                             "--library", library, "--report", out / "report.json"])
    command("native-only", [sys.executable, ROOT / "port/boot/native.py", "--manifest", proof / "manifest.json",
                            "--library", library, "--rom", args.rom.resolve(),
                            "--report", out / "native-only-report.json"])
    print(f"Verified heap/linker startup slice. Report: {out / 'report.json'}", flush=True)


if __name__ == "__main__":
    main()
