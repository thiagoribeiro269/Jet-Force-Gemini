#!/usr/bin/env python3
"""Prepare audio thread startup and complete amInit to the next game boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/audio_players"))
from prepare_players import (AUDIO_SYMBOLS, MANAGER_DEFERRED, MANAGER_ROOTS,
                             MANAGER_SYMBOLS, PLAYER_CALLBACKS, PLAYER_SYMBOLS,
                             prepare_profile)  # noqa: E402

AMINIT_TAIL = ("amGo", "amSetMuteMode", "amVibratoInit",
               "alSurround_OutputType", "alSurround_ReverbSetup",
               "n_alCSPSetMessageQ")
TAIL_CALLS = {
    0x6B4: "amGo", 0x6BC: "amSetMuteMode", 0x6C4: "mmFree",
    0x6CC: "amVibratoInit", 0x6D4: "alSurround_OutputType",
    0x6E0: "alSurround_ReverbSetup", 0x6FC: "osCreateMesgQueue",
    0x708: "n_alCSPSetMessageQ",
}
NEXT_CALL_OFFSET = 0x2C
START_SYMBOLS = ("D_800E97A8_B1758", "D_800F2B10_B1750",
                 "D_800F2B18_B1758", "D_800F2C94_B1784",
                 "D_80105010_B1750", "D_80105018_B1758", "D_800FEC40_B1830")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-audio-start/proof")
    args = parser.parse_args()

    # The audio thread blocks waiting for scheduler messages. Its frame and
    # done handlers remain failing boundaries, as do the player callbacks.
    deferred = tuple(name for name in MANAGER_DEFERRED if name not in AMINIT_TAIL)
    manifest = prepare_profile(
        args.elf, args.rom, args.out,
        implemented=("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew",
                     *AMINIT_TAIL),
        extra_roots=MANAGER_ROOTS,
        extra_deferred=(*deferred, *PLAYER_CALLBACKS),
        extra_imports=("osAiSetFrequency",),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                       *START_SYMBOLS),
    )
    functions = {item["name"]: item for item in manifest["functions"]}
    for name in ("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew",
                 "__amMain", *AMINIT_TAIL):
        if functions[name]["deferred"]:
            raise ValueError(f"Audio startup requires compiled {name}")
    for name in ("amInitAudioMap", "__amHandleFrameMsg", "__amHandleDoneMsg",
                 *PLAYER_CALLBACKS):
        if not functions[name]["deferred"]:
            raise ValueError(f"Audio startup requires deferred {name}")

    sections = {section["overlay"]: section for section in manifest["sections"]}
    def calls(overlay: int) -> dict[int, str]:
        return {group["patch_offset"]: group["callee"]
                for group in sections[overlay]["relocation_groups"]
                if group["kind"] == "call"}

    overlay_calls = calls(25)
    for offset, name in TAIL_CALLS.items():
        if overlay_calls.get(offset) != name:
            raise ValueError(f"Unexpected amInit call at overlay 25 +0x{offset:X}")
    if calls(36).get(NEXT_CALL_OFFSET) != "amInitAudioMap":
        raise ValueError("Expected mainInitRlo to call amInitAudioMap after amInit")

    symbols = manifest["reference_symbols"]
    state_names = (*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS, *START_SYMBOLS)
    manifest["start_profile"] = {
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
        "entry": "amInit", "overlay": 25,
        "entry_offset": functions["amInit"]["offset"],
        "start_function": "amGo", "start_target": functions["amGo"]["vram"],
        "start_call_overlay": 25, "start_call_offset": 0x6B4,
        "thread_function": "__amMain", "thread_target": functions["__amMain"]["vram"],
        "thread_address": symbols["D_800F17C0_B9770"],
        "thread_stack_top": symbols["D_800F17B8_B9768"],
        "thread_client": symbols["D_800E97A8_B1758"],
        "thread_queue": symbols["D_800F1AF4_B9AA4"],
        "amInit_returns": True,
        "overlay25_unloaded_at_boundary": True,
        "tail_calls": {f"0x{offset:X}": name for offset, name in TAIL_CALLS.items()},
        "boundary_function": "amInitAudioMap",
        "boundary_target": functions["amInitAudioMap"]["vram"],
        "boundary_overlay": 0,
        "boundary_target_overlay": 0,
        "boundary_call_overlay": 36,
        "boundary_call_offset": NEXT_CALL_OFFSET,
        "state_symbols": {name: symbols[name] for name in state_names},
        "callbacks": {name: functions[name]["vram"] for name in PLAYER_CALLBACKS},
    }
    manifest["limits"] = [
        "Original amInit completes and starts the audio thread; mainInitRlo stops before amInitAudioMap",
        "Audio thread registers with scheduler then waits for a message; no frames or samples are processed",
        "Frame, done and player callbacks remain explicit failure boundaries",
        "No general FPU-register or FCSR comparison",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Audio-start profile: {len(functions)} entries; full amInit selected, "
          f"amInitAudioMap deferred at overlay 36 +0x{NEXT_CALL_OFFSET:X}.")


if __name__ == "__main__":
    main()
