#!/usr/bin/env python3
"""Prepare the original audio-data initialization up to the audio manager call."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/bootstrap"))
from prepare_bootstrap import prepare_profile  # noqa: E402

# These are real ELF data symbols used by amInit. Keep their names in the
# manifest so the asset/heap audit can inspect the guest memory directly.
AUDIO_SYMBOLS = (
    "hp", "audioHeap", "tuneCSeqs", "tuneCSeqp", "sfxBankPtr", "seqBankPtr",
    "sfxIndex", "seqIndex", "sfxIndexSize", "seqIndexSize", "seqFile",
    "seqLen", "maxSound", "maxSequence", "tuneSeqPlayer",
    "ambientSeqPlayer", "animCtrlQueue", "animCtrlMesgBuf",
)

# Each entry remains an explicit failing boundary. In particular, the audio
# manager cannot report success without its AI, scheduler and thread contracts.
AUDIO_DEFERRED = (
    "amCreateAudioMgr", "n_alCSPNew", "gsSndpNew", "amGo", "amSetMuteMode",
    "amVibratoInit", "alSurround_OutputType", "alSurround_ReverbSetup",
    "n_alCSPSetMessageQ",
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-audio-init/proof")
    args = parser.parse_args()
    manifest = prepare_profile(args.elf, args.rom, args.out,
                               implemented=("amInit",),
                               extra_deferred=AUDIO_DEFERRED,
                               extra_symbols=AUDIO_SYMBOLS)
    functions = {f["name"]: f for f in manifest["functions"]}
    if functions["amInit"]["deferred"] or not functions["amCreateAudioMgr"]["deferred"]:
        raise ValueError("Audio profile must execute amInit and stop before amCreateAudioMgr")
    boundary = functions["amCreateAudioMgr"]
    manifest["audio_profile"] = {
        "entry": "amInit",
        "overlay": 25,
        "entry_offset": 0x308,
        "boundary_function": "amCreateAudioMgr",
        "boundary_overlay": 0,
        "boundary_target": boundary["vram"],
        "boundary_offset": boundary["offset"],
        "boundary_call_overlay": 25,
        "boundary_call_offset": 0x64C,
        "asset_sections": {"offset_table": 0x33, "audio_data": 0x34},
    }
    manifest["limits"] = [
        "Original amInit audio-data prefix; amCreateAudioMgr remains an explicit failure boundary",
        "No AI initialization, audio thread, audio output or complete game boot",
        "Selected relocated JALs follow the original runLink-patched guest instruction",
    ]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Audio-init profile: {len(functions)} functions; amInit compiled, "
          "amCreateAudioMgr deferred.")


if __name__ == "__main__":
    main()
