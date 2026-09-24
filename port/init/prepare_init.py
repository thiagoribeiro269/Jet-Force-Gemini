#!/usr/bin/env python3
"""Prepare original initialization, video buffers, PI reads and decompression."""
import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/events"))
from prepare_events import (EVENT_IMPORTS, SCHEDULER_FUNCTIONS, GRAPHICS_DEFERRED,
                            FUNCTION_ENDS, EVENT_SYMBOLS)
from prepare_threads import MESSAGE_IMPORTS, GAME_FUNCTIONS, THREAD_SYMBOLS
from prepare_boot import ROOTS, CALLBACKS, DEFERRED, closure
from reference import SYMBOLS, PLATFORM_IMPORTS, profile, return_address_patches
from prepare import FUNCTIONS, prepare, OverlayRelocTool

IO_IMPORTS = ("osCreatePiManager", "osPiStartDma", "osInvalDCache", "osWritebackDCacheAll", "osViSetSpecialFeatures")
INIT_FUNCTIONS = ("mainInitGame", "piRomLoad", "piRomLoadSection", "piRomGetFileSize", "piRomGetSectionPtr",
                  "rzipUncompress", "rzipUncompressSize")
INIT_SYMBOLS = ("osTvType", "securitybuffer", "rzip_huft_alloc", "rzip_asset_address", "gAssetsLookupTable",
                "__ASSETS_LUT_START", "__ASSETS_LUT_END", "gAssetsDmaIoMesg", "gDmaMesgQueue", "gDmaMesg",
                "gPIMesgQueue", "gPIMesgBuf", "framebufferPointers_", "currentScreen", "extraScreen", "otherZbuf",
                "framebufferSize", "viFramesPerSecond", "aspectRatioFloat", "hScale", "vScale", "osViMode_custom",
                "D_800A3530_A4130", "D_800A3290_A3E90", "D_800FEB80_B1770", "D_800FF190", "D_800FF1B0",
                "cloneDoneMsgQueue", "gMainMemoryPool", "D_1ECF220")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    p.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    p.add_argument("--out", type=Path, default=ROOT / "build/port-init/proof")
    a = p.parse_args()
    state = profile(a.elf, a.rom)
    imports = tuple(dict.fromkeys((*(n for n in PLATFORM_IMPORTS if n != "romCopy"), *MESSAGE_IMPORTS, *EVENT_IMPORTS, *IO_IMPORTS)))
    deferred = tuple(dict.fromkeys((*DEFERRED, "TrapDanglingJump", *(n for n in GRAPHICS_DEFERRED if n not in imports),
                                   "amStop", "viReset", "rumbleKill", "rumbleTick")))
    names = tuple(dict.fromkeys((*FUNCTIONS, *closure(a.elf, (*ROOTS, *GAME_FUNCTIONS, *SCHEDULER_FUNCTIONS, *INIT_FUNCTIONS),
                                                     imports, deferred, FUNCTION_ENDS),
                                *CALLBACKS, *imports, *deferred)))
    m = prepare(a.elf, a.rom, a.out, functions=names, host_imports=imports, deferred=deferred,
                extra_symbols=(*SYMBOLS, *THREAD_SYMBOLS, *EVENT_SYMBOLS, *INIT_SYMBOLS),
                runtime_patches=return_address_patches(state), function_ends=FUNCTION_ENDS)
    m["boot_profile"] = {"stack_top": state["stack_top"], "loaded_modules": [19, 6, 32, 44]}
    m["event_profile"] = {"scheduler_layout": {"thread": 0xB0, "interrupt_queue": 0x40, "command_queue": 0x78, "frame_count": 0x300}}
    function = next(f for f in m["functions"] if f["name"] == "mainInitGame")
    trap = next(f for f in m["functions"] if f["name"] == "TrapDanglingJump")
    sites = []
    for i, (word,) in enumerate(struct.iter_unpack(">I", state["rom"][function["rom"]:function["rom"] + function["size"]])):
        address = function["vram"] + i * 4
        if word >> 26 == 3 and (((address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)) == trap["vram"]:
            sites.append(address)
    if len(sites) != 1: raise ValueError("Expected one bootstrap overlay call in mainInitGame")
    table = m["reference_symbols"]["D_1ECF220"]
    count = struct.unpack_from(">I", state["rom"], table)[0]
    matches = []
    for index in range(count):
        symbol, packed = struct.unpack_from(">II", state["rom"], table + 4 + index * 8)
        if (packed >> 8) + m["reference_symbols"]["__CODE_SECTION_START"] == sites[0]:
            entry = struct.unpack_from(">I", state["rom"], m["overlay_rom_table"] + symbol * 4)[0]
            matches.append({"symbol_index": symbol, "overlay": entry >> 20, "offset": entry & 0xFFFFF,
                            "name": OverlayRelocTool(str(a.rom)).get_symbol_name(symbol)})
    if len(matches) != 1: raise ValueError("Expected one bootstrap relocation target")
    m["init_profile"] = {"bootstrap_call_site": sites[0], "bootstrap_target": trap["vram"], "bootstrap_overlay": matches[0],
                          "compressed_asset_index": 19, "dma_max_game_chunk": 0x5000}
    m["limits"] = ["Original mainInitGame prefix; stops at unresolved bootstrap overlay call",
                   "Original ROM transfer loop and decompression; host PI completion pumped by session owner",
                   "Framebuffer allocation/clearing only; no RSP/RDP renderer or image presentation",
                   "Reset/shutdown, bootstrap overlay, audio and gameplay paths remain incomplete"]
    (a.out / "manifest.json").write_text(json.dumps(m, indent=2) + "\n")
    print(f"Init profile: {len(names)} entries, {len(imports)} imports, {len(deferred)} deferred targets")


if __name__ == "__main__": main()
