#!/usr/bin/env python3
"""Prepare original dynamic overlay bootstrap through the explicit audio boundary."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/init"))
from prepare_init import (ROOTS, GAME_FUNCTIONS, SCHEDULER_FUNCTIONS, INIT_FUNCTIONS,
                          PLATFORM_IMPORTS, MESSAGE_IMPORTS, EVENT_IMPORTS, IO_IMPORTS,
                          DEFERRED, GRAPHICS_DEFERRED, FUNCTION_ENDS, FUNCTIONS, CALLBACKS,
                          SYMBOLS, THREAD_SYMBOLS, EVENT_SYMBOLS, INIT_SYMBOLS,
                          profile, return_address_patches, closure, prepare)

# Preserve an explicit failure for the unimplemented audio/game subsystems.
BOOTSTRAP_DEFERRED = ("amInit", "amInitAudioMap", "joyInit", "texInitTextures", "modInitModels",
    "explosionFlushBlasts", "arithmeticFunction", "diPrintfInit", "initWeather", "fxInitLines",
    "fxInitLevelEffects", "camlightInit", "packInit", "initFront", "animseqInit", "fxInit",
    "squadsInit", "diRcpTraceInit", "fmvInit", "objGetTable", "mathSeed", "partInitLib",
    "objInitObjects", "fontInit")


def prepare_profile(elf_path, rom_path, out, *, implemented=(), extra_roots=(),
                    extra_deferred=(), extra_symbols=()):
    state = profile(elf_path, rom_path)
    imports = tuple(dict.fromkeys((*(n for n in PLATFORM_IMPORTS if n != "romCopy"),
                                  *MESSAGE_IMPORTS, *EVENT_IMPORTS, *IO_IMPORTS)))
    deferred = tuple(n for n in dict.fromkeys((*DEFERRED, *(n for n in GRAPHICS_DEFERRED if n not in imports),
                      "amStop", "viReset", "rumbleKill", "rumbleTick", *BOOTSTRAP_DEFERRED, *extra_deferred))
                     if n not in implemented)
    roots = (*ROOTS, *GAME_FUNCTIONS, *SCHEDULER_FUNCTIONS, *INIT_FUNCTIONS,
             "TrapDanglingJump", "mainInitRlo", *extra_roots)
    names = tuple(dict.fromkeys((*FUNCTIONS, *closure(elf_path, roots, imports, deferred, FUNCTION_ENDS,
                                                     rom_path=rom_path), *CALLBACKS, *imports, *deferred)))
    m = prepare(elf_path, rom_path, out, functions=names, host_imports=imports, deferred=deferred,
                extra_symbols=(*SYMBOLS, *THREAD_SYMBOLS, *EVENT_SYMBOLS, *INIT_SYMBOLS, *extra_symbols),
                runtime_patches=return_address_patches(state), function_ends=FUNCTION_ENDS)
    metadata = out / "symbols.toml"
    metadata.write_text("live_relocated_calls = true\n\n" + metadata.read_text())
    m["boot_profile"] = {"stack_top": state["stack_top"], "loaded_modules": [19, 6, 32, 44]}
    m["bootstrap_profile"] = {"entry": "mainInitGame", "bootstrap_overlay": 36,
        "bootstrap_function": "mainInitRlo", "boundary_function": "amInit", "boundary_overlay": 25,
        "boundary_offset": 0x308, "boundary_call_overlay": 36, "boundary_call_offset": 0x14,
        "live_relocated_calls": True}
    m["limits"] = ["Original bootstrap through dynamic overlay loading; audio entry remains deferred",
        "Selected relocated JALs follow the actual runLink-patched guest instruction",
        "No audio output, renderer, controls or complete game boot"]
    (out / "manifest.json").write_text(json.dumps(m, indent=2) + "\n")
    return m


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    p.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    p.add_argument("--out", type=Path, default=ROOT / "build/port-bootstrap/proof")
    a = p.parse_args()
    prepare_profile(a.elf, a.rom, a.out)


if __name__ == "__main__": main()
