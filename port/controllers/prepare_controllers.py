#!/usr/bin/env python3
"""Select original controller initialization through the texture boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/audio_map"))
from prepare_map import (AMINIT_TAIL, AUDIO_SYMBOLS, MANAGER_DEFERRED,
                         MANAGER_ROOTS, MANAGER_SYMBOLS, MAP_FUNCTIONS,
                         MAP_SYMBOLS, PLAYER_CALLBACKS, PLAYER_SYMBOLS,
                         START_SYMBOLS, NEXT_CALL_OFFSET,
                         CONTROLLER_CALL_OFFSET, prepare_profile)  # noqa: E402

CONTROLLER_SYMBOLS = (
    "joyMessageQueue", "joyMessageBuf", "joyMessage", "joyStatus",
    "sPlayerID", "enabled", "connected", "numberOfJoypads", "joyfail",
    "__osContGetInitData", "__osContPifRam", "__osMaxControllers",
)
CONTROLLER_IMPORTS = ("osContInit", "osContStartReadData")
BOUNDARY_CALL_OFFSET = 0x40


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-controllers/proof")
    args = parser.parse_args()

    deferred = tuple(name for name in MANAGER_DEFERRED if name not in AMINIT_TAIL)
    manifest = prepare_profile(
        args.elf, args.rom, args.out,
        implemented=("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew",
                     *AMINIT_TAIL, *MAP_FUNCTIONS, "joyInit", "joyResetMap"),
        extra_roots=MANAGER_ROOTS,
        extra_deferred=(*deferred, *PLAYER_CALLBACKS),
        extra_imports=("osAiSetFrequency", *CONTROLLER_IMPORTS),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                       *START_SYMBOLS, *MAP_SYMBOLS, *CONTROLLER_SYMBOLS),
    )
    functions = {item["name"]: item for item in manifest["functions"]}
    for name in ("amInit", *MAP_FUNCTIONS, "joyInit", "joyResetMap"):
        if functions[name]["deferred"]:
            raise ValueError(f"Controller path requires compiled {name}")
    for name in ("texInitTextures", "__amHandleFrameMsg", "__amHandleDoneMsg",
                 *PLAYER_CALLBACKS):
        if not functions[name]["deferred"]:
            raise ValueError(f"Controller path requires deferred {name}")
    for name in CONTROLLER_IMPORTS:
        if not functions[name]["host_import"]:
            raise ValueError(f"Controller path requires host import {name}")
    section = next(item for item in manifest["sections"] if item["overlay"] == 36)
    calls = {group["patch_offset"]: group["callee"]
             for group in section["relocation_groups"] if group["kind"] == "call"}
    if any(calls.get(offset) != name for offset, name in (
        (NEXT_CALL_OFFSET, "amInitAudioMap"),
        (CONTROLLER_CALL_OFFSET, "joyInit"),
        (BOUNDARY_CALL_OFFSET, "texInitTextures"),
    )):
        raise ValueError("Original mainInitRlo audio/controller/texture calls changed")

    symbols = manifest["reference_symbols"]
    state_names = (*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                   *START_SYMBOLS, *MAP_SYMBOLS, *CONTROLLER_SYMBOLS)
    manifest["controller_profile"] = {
        "entry": "mainInitRlo", "overlay": 36,
        "map_function": "amInitAudioMap",
        "map_target": functions["amInitAudioMap"]["vram"],
        "map_call_overlay": 36, "map_call_offset": NEXT_CALL_OFFSET,
        "settings_function": "amGetSfxSettings",
        "settings_target": functions["amGetSfxSettings"]["vram"],
        "reset_function": "amResetAudioMap",
        "reset_target": functions["amResetAudioMap"]["vram"],
        "controller_function": "joyInit",
        "controller_target": functions["joyInit"]["vram"],
        "controller_call_overlay": 36,
        "controller_call_offset": CONTROLLER_CALL_OFFSET,
        "controller_reset_function": "joyResetMap",
        "controller_reset_target": functions["joyResetMap"]["vram"],
        "controller_imports": list(CONTROLLER_IMPORTS),
        "boundary_function": "texInitTextures",
        "boundary_target": functions["texInitTextures"]["vram"],
        "boundary_overlay": 0, "boundary_call_overlay": 36,
        "boundary_call_offset": BOUNDARY_CALL_OFFSET,
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
        "thread_function": "__amMain",
        "thread_target": functions["__amMain"]["vram"],
        "thread_address": symbols["D_800F17C0_B9770"],
        "thread_stack_top": symbols["D_800F17B8_B9768"],
        "thread_client": symbols["D_800E97A8_B1758"],
        "thread_queue": symbols["D_800F1AF4_B9AA4"],
        "allocations": {"sound_entries": 0x5A0, "free_list": 0xA0,
                        "active_list": 0xA0},
        "sound_pool": {"count": 40, "entry_size": 0x24, "size": 0x5A0,
                       "zero_offsets": [0x18, 0x3C, 0x60, 0x84],
                       "zero_stride": 0x90, "zero_groups": 10},
        "pointer_pools": {"count": 40, "entry_size": 4, "size": 0xA0},
        "state_symbols": {name: symbols[name] for name in state_names},
        "callbacks": {name: functions[name]["vram"] for name in PLAYER_CALLBACKS},
    }
    manifest["limits"] = [
        "Original audio map and joyInit complete before texInitTextures",
        "Controller presence is simulated by the host imports; no physical input is read",
        "texInitTextures remains an explicit failure boundary; no rendering",
        "Audio thread waits for scheduler messages; no frames or samples are processed",
        "Frame, done and player callbacks remain explicit failure boundaries",
        "No general FPU-register or FCSR comparison",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Controller profile: {len(functions)} entries; joyInit compiled, "
          f"texInitTextures deferred at overlay 36 +0x{BOUNDARY_CALL_OFFSET:X}.")


if __name__ == "__main__":
    main()
