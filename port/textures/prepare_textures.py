#!/usr/bin/env python3
"""Select original texture and model initialization through the object boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/controllers"))
from prepare_controllers import (AMINIT_TAIL, AUDIO_SYMBOLS, MANAGER_DEFERRED,
                                 MANAGER_ROOTS, MANAGER_SYMBOLS, MAP_FUNCTIONS,
                                 MAP_SYMBOLS, PLAYER_CALLBACKS, PLAYER_SYMBOLS,
                                 START_SYMBOLS, CONTROLLER_SYMBOLS,
                                 CONTROLLER_IMPORTS, NEXT_CALL_OFFSET,
                                 CONTROLLER_CALL_OFFSET, BOUNDARY_CALL_OFFSET,
                                 prepare_profile)  # noqa: E402

PRE_NMI_CALL_OFFSET = 0x48
MODEL_CALL_OFFSET = 0x50
SECOND_PRE_NMI_CALL_OFFSET = 0x58
OBJECT_CALL_OFFSET = 0x60
TEXTURE_SYMBOLS = (
    "D_800FF9C0", "D_800FF9C8", "D_800FF9CC", "D_800FF9D0",
    "D_800FF9D8", "D_800FF9E0", "D_800FF9E4", "D_800FF9E8",
    "D_800FF9EC", "D_800FF9F0", "D_800FF9F4", "D_800FF9F8",
    "D_800FFA0C",
)
MODEL_SYMBOLS = (
    "D_800F6F10_B1750", "D_800F6F14_B1754", "D_800F6F18_B1758",
    "D_800F6F1C_B175C", "D_800F6F20_B1760", "D_800F6F24_B1764",
    "D_800F6F28_B1768", "D_800F6F2C_B176C", "D_800F6F30_B1770",
    "D_800F6F34_B1774", "D_800F6F38_B1778", "D_800F6F3C_B177C",
    "D_800F6F40_B1780", "D_800F6F58_B1798",
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-textures/proof")
    args = parser.parse_args()

    deferred = tuple(name for name in MANAGER_DEFERRED if name not in AMINIT_TAIL)
    manifest = prepare_profile(
        args.elf, args.rom, args.out,
        implemented=("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew",
                     *AMINIT_TAIL, *MAP_FUNCTIONS, "joyInit", "joyResetMap",
                     "texInitTextures", "modInitModels"),
        extra_roots=MANAGER_ROOTS,
        extra_deferred=(*deferred, *PLAYER_CALLBACKS),
        extra_imports=("osAiSetFrequency", *CONTROLLER_IMPORTS),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                       *START_SYMBOLS, *MAP_SYMBOLS, *CONTROLLER_SYMBOLS,
                       *TEXTURE_SYMBOLS, *MODEL_SYMBOLS),
    )
    functions = {item["name"]: item for item in manifest["functions"]}
    for name in ("amInit", *MAP_FUNCTIONS, "joyInit", "joyResetMap",
                 "texInitTextures", "modInitModels", "mainPreNMI"):
        if functions[name]["deferred"]:
            raise ValueError(f"Texture path requires compiled {name}")
    for name in ("objInitObjects", "__amHandleFrameMsg", "__amHandleDoneMsg",
                 *PLAYER_CALLBACKS):
        if not functions[name]["deferred"]:
            raise ValueError(f"Texture path requires deferred {name}")
    for name in CONTROLLER_IMPORTS:
        if not functions[name]["host_import"]:
            raise ValueError(f"Texture path requires host import {name}")
    if functions["objInitObjects"]["offset"] != 0 or not any(
        item["overlay"] == 34 and item["index"] == functions["objInitObjects"]["section"]
        for item in manifest["sections"]
    ):
        raise ValueError("objInitObjects must begin overlay 34")
    section = next(item for item in manifest["sections"] if item["overlay"] == 36)
    calls = {group["patch_offset"]: group["callee"]
             for group in section["relocation_groups"] if group["kind"] == "call"}
    expected = {NEXT_CALL_OFFSET: "amInitAudioMap",
                CONTROLLER_CALL_OFFSET: "joyInit",
                BOUNDARY_CALL_OFFSET: "texInitTextures",
                PRE_NMI_CALL_OFFSET: "mainPreNMI",
                MODEL_CALL_OFFSET: "modInitModels",
                SECOND_PRE_NMI_CALL_OFFSET: "mainPreNMI",
                OBJECT_CALL_OFFSET: "objInitObjects"}
    if any(calls.get(offset) != name for offset, name in expected.items()):
        raise ValueError("Original mainInitRlo texture/model calls changed")

    symbols = manifest["reference_symbols"]
    state_names = (*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                   *START_SYMBOLS, *MAP_SYMBOLS, *CONTROLLER_SYMBOLS,
                   *TEXTURE_SYMBOLS, *MODEL_SYMBOLS)
    manifest["textures_profile"] = {
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
        "texture_function": "texInitTextures",
        "texture_target": functions["texInitTextures"]["vram"],
        "texture_call_overlay": 36,
        "texture_call_offset": BOUNDARY_CALL_OFFSET,
        "pre_nmi_function": "mainPreNMI",
        "pre_nmi_call_overlay": 36,
        "pre_nmi_call_offset": PRE_NMI_CALL_OFFSET,
        "model_function": "modInitModels",
        "model_target": functions["modInitModels"]["vram"],
        "model_call_overlay": 36,
        "model_call_offset": MODEL_CALL_OFFSET,
        "second_pre_nmi_call_overlay": 36,
        "second_pre_nmi_call_offset": SECOND_PRE_NMI_CALL_OFFSET,
        "boundary_function": "objInitObjects",
        "boundary_target": functions["objInitObjects"]["vram"],
        "boundary_overlay": 34,
        "boundary_offset": 0,
        "boundary_call_overlay": 36,
        "boundary_call_offset": OBJECT_CALL_OFFSET,
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
        "texture_asset_sections": {
            "texture_offsets": 3, "texture_data": 2,
            "alternate_texture_offsets": 1, "alternate_texture_data": 0,
            "sprite_offsets": 0x16, "sprite_data": 0x15,
        },
        "model_asset_sections": {"offsets": 0x26, "data": 0x27},
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
        "texture_allocations": [0x15E0, 0x280, 0x320, 0x200, 0x28],
        "model_allocations": [0x2A8, 0x190, 0x2000, 0xA0, 0x800, 0x100],
        "state_symbols": {name: symbols[name] for name in state_names},
        "callbacks": {name: functions[name]["vram"] for name in PLAYER_CALLBACKS},
    }
    manifest["limits"] = [
        "Original audio map, joyInit, texInitTextures and modInitModels complete before objInitObjects",
        "Controller presence is simulated by host imports; no physical input is read",
        "objInitObjects remains an explicit failure boundary; no rendering",
        "Audio thread waits for scheduler messages; no frames or samples are processed",
        "Frame, done and player callbacks remain explicit failure boundaries",
        "No general FPU-register or FCSR comparison",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Texture profile: {len(functions)} entries; texInitTextures and "
          f"modInitModels compiled, objInitObjects deferred at overlay 36 "
          f"+0x{OBJECT_CALL_OFFSET:X}.")


if __name__ == "__main__":
    main()
