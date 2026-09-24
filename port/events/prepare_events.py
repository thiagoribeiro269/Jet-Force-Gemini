#!/usr/bin/env python3
"""Prepare original scheduler/event consumers with explicit platform contracts."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/threads"))
from prepare_threads import MESSAGE_IMPORTS, GAME_FUNCTIONS, THREAD_SYMBOLS
from prepare_boot import ROOTS, CALLBACKS, DEFERRED, closure
from reference import SYMBOLS, PLATFORM_IMPORTS, profile, return_address_patches
from prepare import FUNCTIONS, prepare

EVENT_IMPORTS = ("osSetEventMesg", "osViSetEvent", "osGetCount", "osGetTime", "osSetTime", "osSetTimer",
                 "osCreateViManager", "osViSetMode", "osViBlack", "osSetIntMask")
SCHEDULER_FUNCTIONS = ("osCreateScheduler", "__scMain", "osScAddClient", "osScRemoveClient",
                       "osScGetCmdQ", "osScGetInterruptQ")
GRAPHICS_DEFERRED = ("__scHandleRSP", "__scHandleRDP", "__scExec", "__scAppendList", "__scYield",
                     "osScGetTaskType", "func_800507A4", "func_80050AA4", "__osSpSetStatus", "osDpSetStatus",
                     "segSetBase", "diPrintf", "diPrintfSetXY", "diPrintfAll", "osWritebackDCacheAll",
                     "osSpTaskLoad", "osSpTaskStartGo", "amAudioMgrGetNextFrameCount",
                     "osViGetCurrentFramebuffer", "osViGetNextFramebuffer")
# IDO emitted these local STT_FUNC symbols with size zero. Each explicit end
# is the next ELF function in the same section; ROM bytes are still verified.
FUNCTION_ENDS = {"__scMain": "func_80050670", "__scHandleRSP": "__scHandleRDP",
                 "__scHandleRDP": "__scTaskReady", "__scAppendList": "__scExec", "__scExec": "__scYield"}
EVENT_SYMBOLS = ("__osEventStateTab", "sc", "Time", "osViModeNtscLpn1", "osViModePalLpn1",
                 "osViModeMpalLpn1", "D_800A38CC_A44CC", "gRetraceCounter64")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    p.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    p.add_argument("--out", type=Path, default=ROOT / "build/port-events/proof")
    a = p.parse_args()
    state = profile(a.elf, a.rom)
    imports = (*PLATFORM_IMPORTS, *MESSAGE_IMPORTS, *EVENT_IMPORTS)
    deferred = (*DEFERRED, "TrapDanglingJump", *GRAPHICS_DEFERRED)
    names = tuple(dict.fromkeys((*FUNCTIONS, *closure(a.elf, (*ROOTS, *GAME_FUNCTIONS, *SCHEDULER_FUNCTIONS),
                                                      imports, deferred, FUNCTION_ENDS),
                                *CALLBACKS, *imports, *deferred)))
    manifest = prepare(a.elf, a.rom, a.out, functions=names, host_imports=imports, deferred=deferred,
                       extra_symbols=(*SYMBOLS, *THREAD_SYMBOLS, *EVENT_SYMBOLS),
                       runtime_patches=return_address_patches(state), function_ends=FUNCTION_ENDS)
    manifest["boot_profile"] = {"stack_top": state["stack_top"], "platform_imports": PLATFORM_IMPORTS,
                                "deferred_audio_branches": DEFERRED, "roots": ROOTS,
                                "callbacks": CALLBACKS, "loaded_modules": [19, 6, 32, 44]}
    manifest["thread_profile"] = {"imports": MESSAGE_IMPORTS, "game_functions": GAME_FUNCTIONS}
    manifest["event_profile"] = {"imports": EVENT_IMPORTS, "game_functions": SCHEDULER_FUNCTIONS,
                                 "function_ends": FUNCTION_ENDS, "counter_hz": 46875000,
                                 "scheduler_layout": {"thread": 0xB0, "interrupt_queue": 0x40,
                                                      "command_queue": 0x78, "frame_count": 0x300}}
    manifest["limits"] = ["Original scheduler with headless VI/events; no RSP/RDP execution or image",
                          "Cooperative guest execution; monotonic host time or explicit deterministic test clock",
                          "Graphics/audio task paths remain explicit unresolved dependencies",
                          "Not full boot, mainThread or mainInitGame"]
    (a.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Event profile: {len(names)} entries, {len(imports)} imports, {len(deferred)} deferred targets")


if __name__ == "__main__":
    main()
