#!/usr/bin/env python3
"""Compare player creation against original MIPS through the amGo boundary."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/audio_manager"))
from verify_manager import run
from checks_players import PlayersSession


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.elf, a.rom, a.manifest, a.library, a.report,
        session_class=PlayersSession, boundary_name="amGo")
