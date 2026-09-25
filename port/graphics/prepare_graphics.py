#!/usr/bin/env python3
"""Select the original US model/texture loaders after the objects boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/objects"))
from prepare_objects import (AMINIT_TAIL, AUDIO_SYMBOLS, CONTROLLER_IMPORTS,
                             CONTROLLER_SYMBOLS, MANAGER_DEFERRED, MANAGER_ROOTS,
                             MANAGER_SYMBOLS, MAP_FUNCTIONS, MAP_SYMBOLS,
                             MODEL_SYMBOLS, OBJECT_FUNCTIONS, OBJECT_SYMBOLS,
                             PLAYER_CALLBACKS, PLAYER_SYMBOLS, START_SYMBOLS,
                             TEXTURE_SYMBOLS, main as prepare_objects_main,
                             prepare_profile)

MODEL_ID = 35
TEXTURE_ID = 0x9097

# These are real functions in the direct and failure-path closure of the two
# loaders. The profile generator checks relocations and rejects missing calls.
GRAPHICS_FUNCTIONS = (
    "modLoadModel", "func_8003BF58", "func_8003BE68", "func_8003B640",
    "func_8003CB50", "func_8003C8A8", "func_8003CCC8", "func_8003CD70",
    "modFreeAnim", "makeModelGfx", "func_8003E100", "func_8003E13C",
    "func_8003C6D0", "texLoadTexture", "texFreeTexture",
    "func_80057B8C", "func_80057C50", "texDPTextureX", "texDPInit",
    "texModelTextureLoad", "rzipUncompressSizeROM",
)

GRAPHICS_SYMBOLS = (
    "antsasmrpants", "D_800A2EC0_A3AC0", "D_800A5840",
    "D_800F6F48_B1788", "D_800F6F4C_B178C", "D_800F6F52_B1792",
    "D_800A5830", "D_800A584C", "mmColourTagUnk1",
    "jtbl_800AD30C_ADF0C", "jtbl_800AD324_ADF24", "D_800A52B0",
    "D_800A55B0", "D_800A5838", "D_800A583C", "D_800FFA10",
    "D_800FFA14", "D_800FFA18", "D_800FFA1C", "D_800FFA20",
    "D_800FFA24",
)


def prepare_graphics(elf: Path, rom: Path, out: Path) -> dict:
    """Build a new proof without changing the existing objects proof."""
    out.mkdir(parents=True, exist_ok=True)
    base_out = out / "objects_base"
    previous_argv = sys.argv
    try:
        sys.argv = [str(ROOT / "port/objects/prepare_objects.py"),
                    "--elf", str(elf), "--rom", str(rom), "--out", str(base_out)]
        prepare_objects_main()
    finally:
        sys.argv = previous_argv
    base = json.loads((base_out / "manifest.json").read_text())

    deferred = tuple(name for name in MANAGER_DEFERRED if name not in AMINIT_TAIL)
    manifest = prepare_profile(
        elf, rom, out,
        implemented=("amInit", "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew",
                     *AMINIT_TAIL, *MAP_FUNCTIONS, "joyInit", "joyResetMap",
                     "texInitTextures", "modInitModels", *OBJECT_FUNCTIONS,
                     *GRAPHICS_FUNCTIONS),
        extra_roots=(*MANAGER_ROOTS, *OBJECT_FUNCTIONS, *GRAPHICS_FUNCTIONS),
        extra_deferred=(*deferred, *PLAYER_CALLBACKS),
        extra_imports=("osAiSetFrequency", *CONTROLLER_IMPORTS),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS, *PLAYER_SYMBOLS,
                       *START_SYMBOLS, *MAP_SYMBOLS, *CONTROLLER_SYMBOLS,
                       *TEXTURE_SYMBOLS, *MODEL_SYMBOLS, *OBJECT_SYMBOLS,
                       *GRAPHICS_SYMBOLS),
        function_ends={"Sinf": "Arctanf"},
    )
    functions = {item["name"]: item for item in manifest["functions"]}
    for name in GRAPHICS_FUNCTIONS:
        if name not in functions or functions[name]["deferred"] or functions[name]["host_import"]:
            raise ValueError(f"Graphics proof requires original compiled {name}")
    for name in ("explosionFlushBlasts", "__amHandleFrameMsg",
                 "__amHandleDoneMsg", *PLAYER_CALLBACKS):
        if name not in functions or not functions[name]["deferred"]:
            raise ValueError(f"Graphics proof changed existing failure boundary: {name}")
    for name in GRAPHICS_SYMBOLS:
        if name not in manifest["reference_symbols"]:
            raise ValueError(f"Missing graphics state symbol: {name}")

    # ObjectsSession uses this metadata to stop at the same original boundary.
    manifest["objects_profile"] = base["objects_profile"]
    manifest["graphics_profile"] = {
        **base["objects_profile"],
        "model_id": MODEL_ID,
        "texture_id": TEXTURE_ID,
        "model_function": "modLoadModel",
        "model_target": functions["modLoadModel"]["vram"],
        "texture_function": "texLoadTexture",
        "texture_target": functions["texLoadTexture"]["vram"],
        "model_asset_sections": {"offsets": 0x26, "data": 0x27},
        "texture_asset_sections": {"offsets": 1, "data": 0},
        "animation_asset_sections": {"index": 0x28, "data": 0x29},
    }
    manifest["limits"] = [*base["limits"],
                          "Model 35 and its referenced texture 0x9097 only",
                          "CPU asset loading and RAM graphics commands, not a presented image"]
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-graphics/proof")
    args = parser.parse_args()
    manifest = prepare_graphics(args.elf, args.rom, args.out)
    print(f"Graphics proof: {len(manifest['functions'])} functions; model {MODEL_ID}, "
          f"texture 0x{TEXTURE_ID:04X}; objects boundary retained.")


if __name__ == "__main__":
    main()
