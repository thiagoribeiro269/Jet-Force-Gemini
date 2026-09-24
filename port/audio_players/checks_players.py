#!/usr/bin/env python3
"""Validate the original sequence and sound players before starting audio."""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_manager.py").exists() else HERE.parent / "audio_manager"))
from checks_manager import ManagerSession, run
from player_checks import check_players


class PlayersSession(ManagerSession):
    profile_key = "players_profile"


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report,
        session_class=PlayersSession, extra_checks=check_players, label="audio players")
