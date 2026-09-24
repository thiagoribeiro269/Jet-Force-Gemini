#!/usr/bin/env python3
"""Validate original amInit completion with the audio thread waiting for work."""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_manager.py").exists() else HERE.parent / "audio_manager"))
from checks_manager import ManagerSession, run
from start_checks import check_audio_start


class StartSession(ManagerSession):
    profile_key = "start_profile"
    expected_audio_state = 1

    def bootstrap(self):
        boundary = super().bootstrap()
        # Prove the worker yielded in the empty receive, not just that it ran.
        profile = self.manifest[self.profile_key]
        queue = profile["thread_queue"]
        if self.word(queue) != profile["thread_address"] or self.word(queue + 8) != 0:
            raise AssertionError("Audio thread did not block on its empty scheduler queue")
        return boundary


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report,
        session_class=StartSession, extra_checks=check_audio_start, label="audio startup")
