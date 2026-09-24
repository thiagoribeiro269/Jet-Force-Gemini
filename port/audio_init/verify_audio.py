#!/usr/bin/env python3
"""Compare original audio preparation against MIPS using the bootstrap oracle."""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/bootstrap"))
from verify_bootstrap import run
from checks_audio import AudioSession


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.elf, a.rom, a.manifest, a.library, a.report,
        session_class=AudioSession, boundary_name="amCreateAudioMgr")
