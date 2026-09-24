#!/usr/bin/env python3
"""Prepare audio-manager creation through the next explicit synth-player boundary."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/bootstrap"))
sys.path.insert(0, str(ROOT / "port/audio_init"))
from prepare_bootstrap import prepare_profile  # noqa: E402
from prepare_audio import AUDIO_DEFERRED, AUDIO_SYMBOLS  # noqa: E402

# The manager creates a thread but amGo, which starts it, remains deferred.
# __amDmaNew is called indirectly during n_alSynNew -> alN_PVoiceNew; the
# function pointer it returns is used only when samples are requested.
MANAGER_ROOTS = ("__amMain", "__amDmaNew")
MANAGER_DEFERRED = tuple(name for name in AUDIO_DEFERRED
                         if name not in ("amCreateAudioMgr",)) + (
    "__amHandleFrameMsg", "__amHandleDoneMsg", "n_alClose", "func_80002764",
)

# ELF symbols for the heap, synthesizer, manager buffers/queues and thread.
MANAGER_SYMBOLS = (
    "ALGLOBALS", "n_alGlobals", "n_syn", "osViClock", "viFramesPerSecond",
    "D_800E97A0_B1750", "D_800E97A4_B1754",
    "D_800F2168_BA118", "D_800F216C_BA11C", "D_800F2170_BA120",
    "D_800F17C0_B9770", "D_800F17B8_B9768",
    "D_800F1B0C_B9ABC", "D_800F1AF4_B9AA4", "D_800F2898_BA848",
    "D_800F1B78_B9B28", "D_800F1B8C_B9B3C",
)

HELPER = "func_overlay_25_01900728_1F44000"
MANAGER_CALL_OFFSET = 0x64C
BOUNDARY_CALL_OFFSET = 0x79C


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-audio-manager/proof")
    args = parser.parse_args()

    manifest = prepare_profile(
        args.elf, args.rom, args.out,
        implemented=("amInit", "amCreateAudioMgr"),
        extra_roots=MANAGER_ROOTS,
        extra_deferred=MANAGER_DEFERRED,
        extra_imports=("osAiSetFrequency",),
        extra_symbols=(*AUDIO_SYMBOLS, *MANAGER_SYMBOLS),
    )
    functions = {function["name"]: function for function in manifest["functions"]}
    manager = functions["amCreateAudioMgr"]
    helper = functions[HELPER]
    boundary = functions["n_alCSPNew"]
    if (manager["deferred"] or helper["deferred"] or not boundary["deferred"]):
        raise ValueError("Audio manager must return before the deferred sequence player")
    if any(functions[name]["deferred"] for name in ("__amMain", "__amDmaNew")):
        raise ValueError("Audio manager thread entry and DMA constructor must be callable")
    if not (helper["offset"] <= BOUNDARY_CALL_OFFSET < helper["offset"] + helper["size"]):
        raise ValueError("Sequence-player call is outside the expected overlay helper")

    overlay = next(section for section in manifest["sections"] if section["overlay"] == 25)
    calls = {group["patch_offset"]: group["callee"]
             for group in overlay["relocation_groups"] if group["kind"] == "call"}
    if calls.get(MANAGER_CALL_OFFSET) != "amCreateAudioMgr" or calls.get(BOUNDARY_CALL_OFFSET) != "n_alCSPNew":
        raise ValueError("Overlay callsites no longer match the verified audio path")

    symbols = manifest["reference_symbols"]
    manifest["manager_profile"] = {
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
        "entry": "amInit",
        "overlay": 25,
        "entry_offset": functions["amInit"]["offset"],
        "manager_function": manager["name"],
        "manager_target": manager["vram"],
        "manager_call_overlay": 25,
        "manager_call_offset": MANAGER_CALL_OFFSET,
        "boundary_function": boundary["name"],
        "boundary_target": boundary["vram"],
        "boundary_overlay": 0,
        "boundary_call_overlay": 25,
        "boundary_call_offset": BOUNDARY_CALL_OFFSET,
        "boundary_helper": HELPER,
        "boundary_helper_offset": helper["offset"],
        "state_symbols": {name: symbols[name] for name in (*AUDIO_SYMBOLS, *MANAGER_SYMBOLS)},
        "callbacks": {name: functions[name]["vram"] for name in MANAGER_ROOTS},
    }
    manifest["limits"] = [
        "Original audio manager and synthesizer initialized; sequence player remains an explicit failure boundary",
        "Audio thread created but not started; no frame processing or sound output",
        "DMA callback constructor executes during voice initialization; sample DMA callback remains deferred",
        "No general FPU-register or FCSR comparison",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Audio-manager profile: {len(functions)} entries; manager selected, "
          f"n_alCSPNew deferred at overlay 25 +0x{BOUNDARY_CALL_OFFSET:X}.")


if __name__ == "__main__":
    main()
