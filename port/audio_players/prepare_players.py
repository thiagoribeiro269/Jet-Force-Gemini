#!/usr/bin/env python3
"""Prepare the original sequence and sound players through the amGo boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/bootstrap"))
sys.path.insert(0, str(ROOT / "port/audio_init"))
sys.path.insert(0, str(ROOT / "port/audio_manager"))
from prepare_bootstrap import prepare_profile  # noqa: E402
from prepare_audio import AUDIO_SYMBOLS  # noqa: E402
from prepare_manager import MANAGER_DEFERRED, MANAGER_ROOTS, MANAGER_SYMBOLS  # noqa: E402

HELPER = "func_overlay_25_01900728_1F44000"
SEQ_CALL_OFFSET = 0x79C
SOUND_CALL_OFFSET = 0x6AC
BOUNDARY_CALL_OFFSET = 0x6B4
PLAYER_CALLBACKS = ("func_80086C80", "func_80084848")
PLAYER_SYMBOLS = ("gSoundPlayerPtr", "gSoundStateLists", "gSoundGroupVolume")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-audio-players/proof")
    args = parser.parse_args()

    # The callback addresses are stored in player nodes. Neither callback is
    # invoked while amGo remains deferred, so keep both as explicit boundaries.
    manifest = prepare_profile(
        args.elf, args.rom, args.out,
        implemented=("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew"),
        extra_roots=MANAGER_ROOTS,
        extra_deferred=(*MANAGER_DEFERRED, *PLAYER_CALLBACKS),
        extra_imports=("osAiSetFrequency",),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS),
    )
    functions = {function["name"]: function for function in manifest["functions"]}
    for name in ("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew", HELPER):
        if functions[name]["deferred"]:
            raise ValueError(f"Player path must compile {name}")
    for name in ("amGo", *PLAYER_CALLBACKS):
        if not functions[name]["deferred"]:
            raise ValueError(f"Player path must defer {name}")
    helper = functions[HELPER]
    if not (helper["offset"] <= SEQ_CALL_OFFSET < helper["offset"] + helper["size"]):
        raise ValueError("Sequence-player call is outside its overlay helper")
    overlay = next(section for section in manifest["sections"] if section["overlay"] == 25)
    calls = {group["patch_offset"]: group["callee"]
             for group in overlay["relocation_groups"] if group["kind"] == "call"}
    expected = {SEQ_CALL_OFFSET: "n_alCSPNew", SOUND_CALL_OFFSET: "gsSndpNew",
                BOUNDARY_CALL_OFFSET: "amGo"}
    if any(calls.get(offset) != name for offset, name in expected.items()):
        raise ValueError("Overlay player and amGo call sites differ from the original path")

    symbols = manifest["reference_symbols"]
    state_names = (*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS)
    manifest["players_profile"] = {
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
        "entry": "amInit", "overlay": 25, "entry_offset": functions["amInit"]["offset"],
        "sequence_function": "n_alCSPNew", "sequence_call_offset": SEQ_CALL_OFFSET,
        "sequence_helper": HELPER, "sequence_helper_offset": helper["offset"],
        "sound_function": "gsSndpNew", "sound_call_offset": SOUND_CALL_OFFSET,
        "boundary_function": "amGo", "boundary_target": functions["amGo"]["vram"],
        "boundary_overlay": 0, "boundary_call_overlay": 25,
        "boundary_call_offset": BOUNDARY_CALL_OFFSET,
        "state_symbols": {name: symbols[name] for name in state_names},
        "callbacks": {name: functions[name]["vram"] for name in PLAYER_CALLBACKS},
    }
    manifest["limits"] = [
        "Two original sequence players and one sound player initialized; amGo remains deferred",
        "Audio thread created but not started; no frame processing or sound output",
        "Player event callbacks are recorded but remain deferred",
        "No general FPU-register or FCSR comparison",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Audio-player profile: {len(functions)} entries; player constructors selected, "
          f"amGo deferred at overlay 25 +0x{BOUNDARY_CALL_OFFSET:X}.")


if __name__ == "__main__":
    main()
