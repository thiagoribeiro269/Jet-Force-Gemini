#!/usr/bin/env python3
"""Compare audio-map creation and audio-thread startup with original MIPS."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/audio_start"))
from verify_start import run
from checks_map import MapSession


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.elf, a.rom, a.manifest, a.library, a.report,
        session_class=MapSession, boundary_name="joyInit", compare_a0=True)
