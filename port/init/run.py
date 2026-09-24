#!/usr/bin/env python3
"""Build and validate original initialization, PI transfers and asset decoding."""
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
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    p.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    p.add_argument("--out", type=Path, default=ROOT / "build/port-init")
    p.add_argument("--jobs", type=int, default=2)
    a = p.parse_args()
    if a.jobs < 1: p.error("Use at least one build job")
    for dep in ("elftools", "unicorn"):
        if not importlib.util.find_spec(dep): p.error("Install port/poc/requirements.txt")
    out = a.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    proof, tool, native = out / "proof", out / "jfg-toolchain", out / "native"
    gen = ["-G", "Ninja"] if shutil.which("ninja") else (["-A", "x64"] if os.name == "nt" else [])
    def command(label, args): poc.command(label, args, out)
    command("configure-tool", ["cmake", "-S", ROOT / "port/recompiler", "-B", tool, *gen, "-DCMAKE_BUILD_TYPE=Release"])
    command("build-tool", ["cmake", "--build", tool, "--config", "Release", "--target", "jfg_recomp_tool", "--parallel", a.jobs])
    command("prepare", [sys.executable, ROOT / "port/init/prepare_init.py", "--elf", a.elf.resolve(), "--rom", a.rom.resolve(), "--out", proof])
    if (proof / "generated").exists(): shutil.rmtree(proof / "generated")
    binary = poc.find_artifact(tool, "jfg_recomp_tool.exe" if os.name == "nt" else "jfg_recomp_tool")
    command("recompile", [binary, proof / "recomp.toml"])
    command("configure-native", ["cmake", "-S", ROOT / "port/poc", "-B", native, *gen, "-DCMAKE_BUILD_TYPE=Debug",
             "-DJFG_BOOT_PROFILE=ON", "-DJFG_THREAD_PROFILE=ON", "-DJFG_EVENT_PROFILE=ON", "-DJFG_INIT_PROFILE=ON",
             f"-DJFG_PROOF_DIR={proof.as_posix()}"])
    command("build-native", ["cmake", "--build", native, "--config", "Debug", "--parallel", a.jobs])
    command("smoke", ["ctest", "--test-dir", native, "-C", "Debug", "--output-on-failure"])
    library = poc.find_artifact(native, "jfg_poc.dll" if os.name == "nt" else "libjfg_poc.so")
    common = ["--library", library, "--rom", a.rom.resolve(), "--manifest", proof / "manifest.json"]
    command("initialization", [sys.executable, ROOT / "port/init/checks_init.py", *common, "--report", out / "native-report.json"])
    command("reference", [sys.executable, ROOT / "port/init/verify_init.py", *common, "--elf", a.elf.resolve(), "--report", out / "mips-report.json"])
    print(f"Verified initialization prefix and asset pipeline. Reports: {out}", flush=True)


if __name__ == "__main__": main()
