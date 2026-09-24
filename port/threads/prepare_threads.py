#!/usr/bin/env python3
"""Extend the boot profile with real game message consumers and runtime imports."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/boot"))
from prepare_boot import ROOTS, CALLBACKS, DEFERRED, closure
from reference import SYMBOLS, PLATFORM_IMPORTS, profile, return_address_patches
from prepare import FUNCTIONS, prepare

MESSAGE_IMPORTS = ("osCreateMesgQueue", "osSendMesg", "osJamMesg", "osRecvMesg", "osCreateThread", "osStartThread")
GAME_FUNCTIONS = ("rcpWaitDP", "mainResetPressed")
THREAD_SYMBOLS = ("D_800A4034", "D_800FF1C8", "D_800FF628", "refractDoneMsgQueue",
                  "blurTaskActive", "refractTaskActive", "cloneTaskActive", "resetMsgQueue", "resetPressed")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    p.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    p.add_argument("--out", type=Path, default=ROOT / "build/port-threads/proof")
    a = p.parse_args()
    state = profile(a.elf, a.rom)
    imports = (*PLATFORM_IMPORTS, *MESSAGE_IMPORTS)
    deferred = (*DEFERRED, "TrapDanglingJump")
    names = tuple(dict.fromkeys((*FUNCTIONS, *closure(a.elf, (*ROOTS, *GAME_FUNCTIONS), imports, deferred),
                                *CALLBACKS, *imports, *deferred)))
    manifest = prepare(a.elf, a.rom, a.out, functions=names, host_imports=imports,
                       deferred=deferred, extra_symbols=(*SYMBOLS, *THREAD_SYMBOLS),
                       runtime_patches=return_address_patches(state))
    manifest["boot_profile"] = {"stack_top": state["stack_top"], "platform_imports": PLATFORM_IMPORTS,
                                "deferred_audio_branches": DEFERRED, "roots": ROOTS,
                                "callbacks": CALLBACKS, "loaded_modules": [19, 6, 32, 44]}
    manifest["thread_profile"] = {"imports": MESSAGE_IMPORTS, "game_functions": GAME_FUNCTIONS,
                                  "max_threads_per_session": 32, "clone_task_path_deferred": True}
    manifest["limits"] = ["Cooperative thread/message integration, not full boot or mainThread",
                          "DP/blur/refraction completion messages are controlled test inputs, not GPU work",
                          "Serialized guest execution; no hardware preemption, timers, rendering or audio output",
                          "Three explicit deferred paths remain unsupported"]
    (a.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Thread profile: {len(names)} entries, {len(imports)} host imports, {len(deferred)} deferred paths")


if __name__ == "__main__":
    main()
