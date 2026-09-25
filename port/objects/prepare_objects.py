#!/usr/bin/env python3
"""Select original object initialization through the explosion boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/textures"))
from prepare_textures import (AMINIT_TAIL, AUDIO_SYMBOLS, MANAGER_DEFERRED,
                              MANAGER_ROOTS, MANAGER_SYMBOLS, MAP_FUNCTIONS,
                              MAP_SYMBOLS, PLAYER_CALLBACKS, PLAYER_SYMBOLS,
                              START_SYMBOLS, CONTROLLER_SYMBOLS,
                              CONTROLLER_IMPORTS, TEXTURE_SYMBOLS, MODEL_SYMBOLS,
                              prepare_profile)  # noqa: E402

OBJECT_FUNCTIONS = (
    "objInitObjects", "mmAllocRegion", "hitInit", "lightDefaultObjectLight",
    "lightSetObjectLight", "mathOneFloatRPY", "Cosf", "Sinf", "resetVars",
    "objInitExplosions", "func_overlay_34_022002C8_1F54D90",
)
OBJECT_SYMBOLS = (
    "objregion", "deletelist", "playerlist", "animplayerlist", "objindex",
    "objindex_max", "objTempBufSize", "objTempBuf", "RomTab", "MaxTypes",
    "objdeflist", "objdefno", "Ftables", "Fmax", "Findex", "ObjList",
    "NoAddObjList", "obj_olddt", "explosionNoTypes", "explosionTypeData",
    "explosionTypes", "swpolygons",
    "D_801047E4_B1754", "D_801047E8_B1758", "D_801047EC_B175C",
    "D_800F65E8", "D_800F65E0", "D_800F3860", "D_800F386C",
    "D_800F3870", "D_800F3908", "D_800F3910", "D_800F391C",
    "ObjListCount", "D_800F38AC", "D_800F38B8", "D_800F38BC",
    "D_800F38C4", "D_800F38C0", "D_800F38C2", "D_800F3948",
    "gMemoryPools", "gNumberOfMemoryPools", "__ASSETS_LUT_START",
    "__ASSETS_LUT_END",
)
OBJECT_CALL_OFFSET = 0x60
FREE_OBJECT_OVERLAY_OFFSET = 0x68
PRE_NMI_OFFSET = 0x70
BOUNDARY_CALL_OFFSET = 0x78


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-objects/proof")
    args = parser.parse_args()

    deferred = tuple(name for name in MANAGER_DEFERRED if name not in AMINIT_TAIL)
    manifest = prepare_profile(
        args.elf, args.rom, args.out,
        implemented=("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew",
                     *AMINIT_TAIL, *MAP_FUNCTIONS, "joyInit", "joyResetMap",
                     "texInitTextures", "modInitModels", *OBJECT_FUNCTIONS),
        extra_roots=(*MANAGER_ROOTS, *OBJECT_FUNCTIONS),
        extra_deferred=(*deferred, *PLAYER_CALLBACKS),
        extra_imports=("osAiSetFrequency", *CONTROLLER_IMPORTS),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                       *START_SYMBOLS, *MAP_SYMBOLS, *CONTROLLER_SYMBOLS,
                       *TEXTURE_SYMBOLS, *MODEL_SYMBOLS, *OBJECT_SYMBOLS),
        function_ends={"Sinf": "Arctanf"},
    )
    functions = {item["name"]: item for item in manifest["functions"]}
    for name in ("amInit", *MAP_FUNCTIONS, "joyInit", "joyResetMap",
                 "texInitTextures", "modInitModels", *OBJECT_FUNCTIONS):
        if name not in functions or functions[name]["deferred"]:
            raise ValueError(f"Object path requires compiled {name}")
    for name in ("explosionFlushBlasts", "__amHandleFrameMsg",
                 "__amHandleDoneMsg", *PLAYER_CALLBACKS):
        if name not in functions or not functions[name]["deferred"]:
            raise ValueError(f"Object path requires deferred {name}")
    section = next(item for item in manifest["sections"] if item["overlay"] == 36)
    calls = {group["patch_offset"]: group["callee"]
             for group in section["relocation_groups"] if group["kind"] == "call"}
    expected = {OBJECT_CALL_OFFSET: "objInitObjects",
                FREE_OBJECT_OVERLAY_OFFSET: "runlinkFreeCode",
                PRE_NMI_OFFSET: "mainPreNMI",
                BOUNDARY_CALL_OFFSET: "explosionFlushBlasts"}
    if any(calls.get(offset) != name for offset, name in expected.items()):
        raise ValueError("Original mainInitRlo object/free/boundary calls changed")

    symbols = manifest["reference_symbols"]
    missing = set(OBJECT_SYMBOLS) - symbols.keys()
    if missing:
        raise ValueError(f"Missing object state symbols: {sorted(missing)}")
    state_names = (*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                   *START_SYMBOLS, *MAP_SYMBOLS, *CONTROLLER_SYMBOLS,
                   *TEXTURE_SYMBOLS, *MODEL_SYMBOLS, *OBJECT_SYMBOLS)
    manifest["objects_profile"] = {
        "entry": "mainInitRlo", "overlay": 36,
        "map_function": "amInitAudioMap",
        "map_target": functions["amInitAudioMap"]["vram"],
        "map_call_overlay": 36, "map_call_offset": 0x2C,
        "settings_function": "amGetSfxSettings",
        "settings_target": functions["amGetSfxSettings"]["vram"],
        "reset_function": "amResetAudioMap",
        "reset_target": functions["amResetAudioMap"]["vram"],
        "controller_function": "joyInit",
        "controller_target": functions["joyInit"]["vram"],
        "controller_call_overlay": 36, "controller_call_offset": 0x34,
        "controller_reset_function": "joyResetMap",
        "controller_reset_target": functions["joyResetMap"]["vram"],
        "controller_imports": list(CONTROLLER_IMPORTS),
        "texture_function": "texInitTextures",
        "texture_target": functions["texInitTextures"]["vram"],
        "texture_call_overlay": 36, "texture_call_offset": 0x40,
        "pre_nmi_function": "mainPreNMI",
        "pre_nmi_call_overlay": 36, "pre_nmi_call_offset": 0x48,
        "model_function": "modInitModels",
        "model_target": functions["modInitModels"]["vram"],
        "model_call_overlay": 36, "model_call_offset": 0x50,
        "second_pre_nmi_call_overlay": 36,
        "second_pre_nmi_call_offset": 0x58,
        "object_function": "objInitObjects",
        "object_target": functions["objInitObjects"]["vram"],
        "object_overlay": 34,
        "object_call_overlay": 36,
        "object_call_offset": OBJECT_CALL_OFFSET,
        "object_free_call_offset": FREE_OBJECT_OVERLAY_OFFSET,
        "post_object_pre_nmi_call_offset": PRE_NMI_OFFSET,
        "boundary_function": "explosionFlushBlasts",
        "boundary_target": functions["explosionFlushBlasts"]["vram"],
        "boundary_overlay": 0,
        "boundary_call_overlay": 36,
        "boundary_call_offset": BOUNDARY_CALL_OFFSET,
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
        "object_asset_sections": {
            "object_table": 0x2E, "object_index": 0x30,
            "object_section_18": 0x18, "object_section_19": 0x19,
            "explosion_offsets": 0x3F, "explosion_data": 0x40,
        },
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
        "Original objInitObjects, overlay 34 release and mainPreNMI complete before explosionFlushBlasts",
        "explosionFlushBlasts remains an explicit failure boundary",
        "Controller presence is simulated by host imports; no physical input is read",
        "Audio thread waits for scheduler messages; no frames or samples are processed",
        "No graphics renderer or display output",
        "Frame, done and player callbacks remain explicit failure boundaries",
        "No general FPU-register or FCSR comparison",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Object profile: {len(functions)} entries; objInitObjects compiled, "
          f"explosionFlushBlasts deferred at overlay 36 +0x{BOUNDARY_CALL_OFFSET:X}.")


if __name__ == "__main__":
    main()
