#!/usr/bin/env python3
"""Select the original audio-map initialization through the controller boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/audio_start"))
from prepare_start import (AMINIT_TAIL, AUDIO_SYMBOLS, MANAGER_DEFERRED,
                           MANAGER_ROOTS, MANAGER_SYMBOLS, PLAYER_CALLBACKS,
                           PLAYER_SYMBOLS, START_SYMBOLS, NEXT_CALL_OFFSET,
                           prepare_profile)  # noqa: E402

MAP_FUNCTIONS = ("amInitAudioMap", "amGetSfxSettings", "amResetAudioMap")
MAP_SYMBOLS = (
    "D_800F35F0", "D_800F29F8_B1758", "D_800F2A00_B1760",
    "D_800F35F4", "D_800F29FC_B175C", "D_800A0800_A1400",
    "D_800F3604", "sfxIndex", "sfxIndexSize", "maxSound",
    "D_800F2A48_B17A8", "D_800F2A58_B17B8", "D_800F2A68_B17C8",
    "D_800F2A78_B17D8", "D_800F2A88_B17E8", "D_800F2A98_B17F8",
    "D_800F2AA8_B1808", "D_800F2AB8_B1818", "D_800F2AC8_B1828",
    "D_800F2AD8_B1838", "D_800F2AE8_B1848", "D_800F2AF8_B1858",
)
CONTROLLER_CALL_OFFSET = 0x34


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-audio-map/proof")
    args = parser.parse_args()

    deferred = tuple(name for name in MANAGER_DEFERRED if name not in AMINIT_TAIL)
    manifest = prepare_profile(
        args.elf, args.rom, args.out,
        implemented=("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew",
                     *AMINIT_TAIL, *MAP_FUNCTIONS),
        extra_roots=MANAGER_ROOTS,
        extra_deferred=(*deferred, *PLAYER_CALLBACKS),
        extra_imports=("osAiSetFrequency",),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                       *START_SYMBOLS, *MAP_SYMBOLS),
    )
    functions = {item["name"]: item for item in manifest["functions"]}
    for name in ("amInit", *MAP_FUNCTIONS):
        if functions[name]["deferred"]:
            raise ValueError(f"Audio-map path requires compiled {name}")
    for name in ("joyInit", "__amHandleFrameMsg", "__amHandleDoneMsg",
                 *PLAYER_CALLBACKS):
        if not functions[name]["deferred"]:
            raise ValueError(f"Audio-map path requires deferred {name}")
    section = next(item for item in manifest["sections"] if item["overlay"] == 36)
    calls = {group["patch_offset"]: group["callee"]
             for group in section["relocation_groups"] if group["kind"] == "call"}
    if calls.get(NEXT_CALL_OFFSET) != "amInitAudioMap" or calls.get(CONTROLLER_CALL_OFFSET) != "joyInit":
        raise ValueError("Original mainInitRlo map/controller call sites changed")

    symbols = manifest["reference_symbols"]
    state_names = (*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                   *START_SYMBOLS, *MAP_SYMBOLS)
    manifest["map_profile"] = {
        "entry": "mainInitRlo", "overlay": 36,
        "map_function": "amInitAudioMap",
        "map_target": functions["amInitAudioMap"]["vram"],
        "map_call_overlay": 36, "map_call_offset": NEXT_CALL_OFFSET,
        "settings_function": "amGetSfxSettings",
        "settings_target": functions["amGetSfxSettings"]["vram"],
        "reset_function": "amResetAudioMap",
        "reset_target": functions["amResetAudioMap"]["vram"],
        "boundary_function": "joyInit", "boundary_target": functions["joyInit"]["vram"],
        "boundary_overlay": 0, "boundary_call_overlay": 36,
        "boundary_call_offset": CONTROLLER_CALL_OFFSET,
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
        "thread_function": "__amMain",
        "thread_target": functions["__amMain"]["vram"],
        "thread_address": symbols["D_800F17C0_B9770"],
        "thread_stack_top": symbols["D_800F17B8_B9768"],
        "thread_client": symbols["D_800E97A8_B1758"],
        "thread_queue": symbols["D_800F1AF4_B9AA4"],
        "allocations": {"sound_entries": 0x5A0, "free_list": 0xA0,
                        "active_list": 0xA0},
        "sound_pool": {"count": 40, "entry_size": 0x24,
                       "size": 0x5A0, "zero_offsets": [0x18, 0x3C, 0x60, 0x84],
                       "zero_stride": 0x90, "zero_groups": 10},
        "pointer_pools": {"count": 40, "entry_size": 4, "size": 0xA0},
        "state_symbols": {name: symbols[name] for name in state_names},
        "callbacks": {name: functions[name]["vram"] for name in PLAYER_CALLBACKS},
    }
    manifest["limits"] = [
        "Original audio map and its three allocations complete before joyInit",
        "joyInit remains an explicit failure boundary; controller APIs are not modeled",
        "Audio thread waits for scheduler messages; no frames or samples are processed",
        "Frame, done and player callbacks remain explicit failure boundaries",
        "No general FPU-register or FCSR comparison",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Audio-map profile: {len(functions)} entries; map compiled, "
          f"joyInit deferred at overlay 36 +0x{CONTROLLER_CALL_OFFSET:X}.")


if __name__ == "__main__":
    main()
