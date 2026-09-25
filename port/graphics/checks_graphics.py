#!/usr/bin/env python3
"""Load the selected real JFG model/texture and inspect their generated commands."""
import argparse
import json
from pathlib import Path
import platform
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_objects.py").exists() else HERE.parent / "objects"))
from checks_objects import ObjectsSession
from object_checks import check_objects
from checks_manager import require, load_native_library
from graphics_asset_checks import check_graphics_assets


class GraphicsSession(ObjectsSession):
    profile_key = "graphics_profile"


def run(library_path, rom_path, manifest_path, report_path):
    lib = load_native_library(library_path)
    rom, manifest = rom_path.read_bytes(), json.loads(manifest_path.read_text())
    cases = []
    for tv in (1, 0, 2):
        session = GraphicsSession(lib, rom, manifest, tv, dirty_heap=True)
        try:
            boundary = session.bootstrap()
            check_objects(session)
            before = session.io()
            result = session.run_function("modLoadModel", 35, 0)[2] & 0xFFFFFFFF
            require(0x80000000 <= result < 0x80400000, "Original model loader did not return an instance")
            assets = check_graphics_assets(session, result)
            after = session.io()
            require(after["pending"] == 0 and after["rejected"] == 0, "Asset loader left invalid/pending PI work")
        finally:
            joined = session.close()
        require(joined == 4, "Asset test did not retire all four workers")
        cases.append({"name": f"real_graphics_assets_tv_{tv}", "status": "passed", "bootstrap_boundary": boundary,
                      "model_id": 35, "texture_id": "0x9097", "instance": f"0x{result:08X}",
                      "pi_transfers": after["completed"] - before["completed"],
                      "assets": assets, "threads_joined": joined})
        print(f"PASS original model 35 and texture 0x9097 TV {tv}; {joined} workers joined", flush=True)
    report = {"status": "passed", "platform": platform.platform(), "cases": cases,
              "limits": ["Original CPU loaders and generated command data only; no RT64, RSP execution, GPU or rendered frame",
                         "One selected model/texture pair; no general model, animation or texture-format coverage"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    run(args.library, args.rom, args.manifest, args.report)
