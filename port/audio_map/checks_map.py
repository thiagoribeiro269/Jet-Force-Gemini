#!/usr/bin/env python3
"""Validate the original audio map through the joyInit boundary."""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_start.py").exists() else HERE.parent / "audio_start"))
from checks_start import StartSession
from checks_manager import run
from map_checks import check_audio_map


class MapSession(StartSession):
    profile_key = "map_profile"


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report,
        session_class=MapSession, extra_checks=check_audio_map, label="audio map")
