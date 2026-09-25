#!/usr/bin/env python3
"""Build and compare original controller initialization through texInitTextures."""
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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-controllers")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("Use at least one build job")
    for dependency in ("elftools", "unicorn"):
        if not importlib.util.find_spec(dependency):
            parser.error("Install port/poc/requirements.txt")

    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    proof = out / "proof"
    tool = ROOT / "build/port-bootstrap/jfg-toolchain"
    native = out / "native"
    generator = (["-G", "Ninja"] if shutil.which("ninja") else
                 (["-A", "x64"] if os.name == "nt" else []))

    def command(label, arguments):
        poc.command(label, arguments, out)

    command("configure-tool", ["cmake", "-S", ROOT / "port/recompiler", "-B", tool,
                               *generator, "-DCMAKE_BUILD_TYPE=Release"])
    command("build-tool", ["cmake", "--build", tool, "--config", "Release",
                           "--target", "jfg_recomp_tool", "--parallel", args.jobs])
    command("prepare", [sys.executable, ROOT / "port/controllers/prepare_controllers.py",
                        "--elf", args.elf.resolve(), "--rom", args.rom.resolve(),
                        "--out", proof])
    if (proof / "generated").exists():
        shutil.rmtree(proof / "generated")
    binary = poc.find_artifact(tool, "jfg_recomp_tool.exe" if os.name == "nt" else "jfg_recomp_tool")
    command("recompile", [binary, proof / "recomp.toml"])
    command("configure-native", ["cmake", "-S", ROOT / "port/poc", "-B", native,
                                 *generator, "-DCMAKE_BUILD_TYPE=Debug",
                                 "-DJFG_BOOT_PROFILE=ON", "-DJFG_THREAD_PROFILE=ON",
                                 "-DJFG_EVENT_PROFILE=ON", "-DJFG_INIT_PROFILE=ON",
                                 "-DJFG_AUDIO_MANAGER_PROFILE=ON",
                                 "-DJFG_CONTROLLER_PROFILE=ON",
                                 f"-DJFG_PROOF_DIR={proof.as_posix()}"])
    command("build-native", ["cmake", "--build", native, "--config", "Debug",
                             "--parallel", args.jobs])
    command("smoke", ["ctest", "--test-dir", native, "-C", "Debug", "--output-on-failure"])
    library = poc.find_artifact(native, "jfg_poc.dll" if os.name == "nt" else "libjfg_poc.so")
    common = ["--library", library, "--rom", args.rom.resolve(),
              "--manifest", proof / "manifest.json"]
    command("protocol", [sys.executable, ROOT / "port/controllers/checks_protocol.py",
                         *common, "--report", out / "protocol-report.json"])
    command("initialization", [sys.executable, ROOT / "port/controllers/checks_controllers.py",
                               *common, "--report", out / "native-report.json"])
    command("reference", [sys.executable, ROOT / "port/controllers/verify_controllers.py",
                          *common, "--elf", args.elf.resolve(),
                          "--report", out / "mips-report.json"])
    print(f"Verified controller initialization through texInitTextures. Reports: {out}", flush=True)


if __name__ == "__main__":
    main()
