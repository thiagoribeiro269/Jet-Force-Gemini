#!/usr/bin/env python3
"""Check original texture/model directory initialization without rendering."""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_controllers.py").exists() else HERE.parent / "controllers"))
from checks_controllers import ControllerSession
from checks_manager import run, require
from texture_checks import check_textures


class TexturesSession(ControllerSession):
    profile_key = "textures_profile"

    def bootstrap(self):
        result = super().bootstrap()
        table = self.word(self.symbols["overlayTable"])
        base34 = self.word(table + 34 * 32)
        site = int(result["call_site"], 16)
        instruction = self.word(site)
        require(base34 == int(result["target"], 16) and instruction >> 26 == 3 and
                (((site + 4) & 0xF0000000) | ((instruction & 0x03FFFFFF) << 2)) == base34,
                "Original loader did not patch the object-initialization call")
        result["overlay34"] = f"0x{base34:08X}"
        return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    run(args.library, args.rom, args.manifest, args.report,
        session_class=TexturesSession, extra_checks=check_textures,
        label="texture/model directory initialization")
