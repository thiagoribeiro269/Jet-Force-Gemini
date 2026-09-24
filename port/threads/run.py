#!/usr/bin/env python3
"""Build and verify the cooperative thread/message profile from local inputs."""
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
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-threads")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("Use at least one build job")
    for dependency in ("elftools", "unicorn"):
        if not importlib.util.find_spec(dependency):
            parser.error("Install port/poc/requirements.txt into the active Python environment")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    toolchain, proof, native = out / "jfg-toolchain", out / "proof", out / "native"
    generator = ["-G", "Ninja"] if shutil.which("ninja") else (["-A", "x64"] if os.name == "nt" else [])

    def command(label, arguments):
        poc.command(label, arguments, out)

    command("configure-tool", ["cmake", "-S", ROOT / "port/recompiler", "-B", toolchain,
                               *generator, "-DCMAKE_BUILD_TYPE=Release"])
    command("build-tool", ["cmake", "--build", toolchain, "--config", "Release",
                           "--target", "jfg_recomp_tool", "--parallel", args.jobs])
    command("prepare", [sys.executable, ROOT / "port/threads/prepare_threads.py", "--elf", args.elf.resolve(),
                         "--rom", args.rom.resolve(), "--out", proof])
    if (proof / "generated").exists():
        shutil.rmtree(proof / "generated")
    tool = poc.find_artifact(toolchain, "jfg_recomp_tool.exe" if os.name == "nt" else "jfg_recomp_tool")
    command("recompile", [tool, proof / "recomp.toml"])
    command("configure-native", ["cmake", "-S", ROOT / "port/poc", "-B", native, *generator,
                                  "-DCMAKE_BUILD_TYPE=Debug", "-DJFG_BOOT_PROFILE=ON", "-DJFG_THREAD_PROFILE=ON",
                                  f"-DJFG_PROOF_DIR={proof.as_posix()}"])
    command("build-native", ["cmake", "--build", native, "--config", "Debug", "--parallel", args.jobs])
    command("smoke", ["ctest", "--test-dir", native, "-C", "Debug", "--output-on-failure"])
    library = poc.find_artifact(native, "jfg_poc.dll" if os.name == "nt" else "libjfg_poc.so")
    common = ["--library", library, "--manifest", proof / "manifest.json"]
    command("boot-regression", [sys.executable, ROOT / "port/boot/verify_boot.py", *common,
                                 "--report", out / "boot-regression-report.json"])
    command("thread-checks", [sys.executable, ROOT / "port/threads/checks.py", *common,
                               "--rom", args.rom.resolve(), "--report", out / "native-report.json"])
    command("game-reference", [sys.executable, ROOT / "port/threads/verify_game.py", *common,
                                "--elf", args.elf.resolve(), "--rom", args.rom.resolve(),
                                "--report", out / "game-reference-report.json"])
    print(f"Verified thread/message profile. Reports: {out}", flush=True)


if __name__ == "__main__":
    main()
