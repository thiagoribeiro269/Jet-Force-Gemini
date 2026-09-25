#!/usr/bin/env python3
"""Check the planned object-initialization block and temporary overlay lifetime."""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_textures.py").exists() else HERE.parent / "textures"))
from checks_textures import ControllerSession
from checks_manager import require, run
from object_checks import check_objects


class ObjectsSession(ControllerSession):
    profile_key = "objects_profile"

    def bootstrap(self):
        result = super().bootstrap()
        table = self.word(self.symbols["overlayTable"])
        retired = {}
        for overlay, name in ((33, "objInitExplosions"), (34, "objInitObjects")):
            section = next(s for s in self.manifest["sections"] if s["overlay"] == overlay)
            loads = [e for e in self.native.events() if e[0] == 1 and e[1] == section["rom"]]
            require(len(loads) == 1 and loads[0][2] != 0, f"Expected one original load of overlay {overlay}")
            require(self.word(table + overlay * 32) == 0, f"Temporary overlay {overlay} remains loaded")
            require(self.lib.jfg_poc_address(name.encode()) == 0,
                    f"Retired function {name} remains registered")
            retired[str(overlay)] = f"0x{loads[0][2]:08X}"
        result["retired_overlays"] = retired
        return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    run(args.library, args.rom, args.manifest, args.report,
        session_class=ObjectsSession, extra_checks=check_objects,
        label="object-initialization block")
