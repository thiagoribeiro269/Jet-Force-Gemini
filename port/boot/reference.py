"""Original JFG startup/linker execution with explicit host-platform contracts.

This module is an independent MIPS test oracle, not the proposed game runtime.
Only ROM DMA and cache maintenance are adapted; allocation and linking execute
the original game instructions.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE
from unicorn import mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/poc"))
from prepare import ROM_SHA1, rom_offset, unique_symbol  # noqa: E402
from verify import Oracle, physical, sx32, RAM_SIZE, RETURN  # noqa: E402

PLATFORM_IMPORTS = ("romCopy", "osWritebackDCache", "osInvalICache")
SYMBOLS = (
    "RevealReturnAddresses", "mmInit", "runlinkInitialise", "runlinkDownloadCode",
    "runlinkUnloadOverlay", "runlinkIsModuleLoaded", "mmSetDelay", "mmGetDelay",
    "romCopy", "osWritebackDCache", "osInvalICache", "gThread3Stack",
    "mmExtendedRam", "mmEndRam", "FreeRAM", "gMainMemoryPool", "gMemoryPools",
    "gNumberOfMemoryPools", "overlayTable", "overlayCount", "mainRelocTable",
    "mainRelocCount", "D_800FF838", "gPendingOverlayLoads", "D_800A3370_A3F70",
    "__CODE_SECTION_START", "__DATA_SECTION_START", "__BSS_SECTION_START", "__BSS_SECTION_END",
)


def profile(elf_path, rom_path):
    rom = Path(rom_path).read_bytes()
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise ValueError("Incorrect US ROM")
    with Path(elf_path).open("rb") as file:
        elf = ELFFile(file)
        symtab = elf.get_section_by_name(".symtab")
        symbols = {name: unique_symbol(symtab, name)["st_value"] for name in SYMBOLS}
        main = elf.get_section_by_name(".main")
        offset = rom_offset(elf, main)
        if main.data() != rom[offset:offset + main["sh_size"]]:
            raise ValueError("ELF main image differs from the ROM")
        stack = unique_symbol(symtab, "gThread3Stack")
        image = bytearray(RAM_SIZE)
        start = physical(main["sh_addr"])
        image[start:start + main["sh_size"]] = main.data()
        return {"rom": rom, "image": image, "symbols": symbols,
                "stack_top": stack["st_value"] + stack["st_size"] - 8,
                "main_vram": main["sh_addr"], "main_rom": offset}


def return_address_patches(state):
    """Model the five candidate scans performed by RevealReturnAddresses.

    The test compares this metadata against execution of the original routine.
    The immutable input ROM is never edited.
    """
    image, symbols = state["image"], state["symbols"]
    patches = []
    for index in range(5):
        table = symbols["D_800A3370_A3F70"] - index * 4
        function = struct.unpack_from(">I", image, physical(table))[0]
        for word_index in range(64):
            address = function + word_index * 4
            old = struct.unpack_from(">I", image, physical(address))[0]
            if old >> 26 == 9 and old & 0xFFFF == 0x666:
                register = (old >> 16) & 31
                new = (31 << 21) | (register << 11) | 0x25
                patches.append({"vram": address, "before": old, "after": new})
                break
        # The original loop also leaves a candidate unchanged if its first
        # 64 instructions contain no marker (one entry is stale in this ROM).
    return patches


class BootOracle(Oracle):
    def __init__(self, state, extended=False):
        super().__init__(state["image"])
        self.state = state
        self.events = []
        self.write(state["symbols"]["mmExtendedRam"], bytes([bool(extended)]))
        for name in PLATFORM_IMPORTS:
            address = sx32(state["symbols"][name])
            self.cpu.hook_add(UC_HOOK_CODE, self._platform_call, name, begin=address, end=address)

    def _platform_call(self, cpu, pc, instruction_size, name):
        args = [cpu.reg_read(getattr(mips_const, f"UC_MIPS_REG_{i}")) & 0xFFFFFFFF for i in (4, 5, 6)]
        if name == "romCopy":
            source, destination, length = args
            if source + length > len(self.state["rom"]) or physical(destination) + length > RAM_SIZE:
                raise ValueError("Original startup requested an out-of-bounds ROM transfer")
            self.write(destination, self.state["rom"][source:source + length])
            self.events.append((1, source, destination, length))
        else:
            address, length = args[:2]
            if physical(address) + length > RAM_SIZE:
                raise ValueError("Original startup requested an invalid cache range")
            self.events.append((2 if name == "osWritebackDCache" else 3, address, length, 0))
        cpu.reg_write(mips_const.UC_MIPS_REG_PC, cpu.reg_read(mips_const.UC_MIPS_REG_RA))

    def invoke(self, name, *args):
        registers = [0] * 32
        for i, value in enumerate(args, 4):
            registers[i] = sx32(value)
        registers[29] = sx32(self.state["stack_top"])
        registers[31] = sx32(RETURN)
        return self.call(self.state["symbols"][name], registers, budget=3000000)

    def u32(self, address):
        return struct.unpack(">I", self.cpu.mem_read(physical(address), 4))[0]


if __name__ == "__main__":
    state = profile(ROOT / "build/jfg.us.elf", ROOT / "baseroms/baserom.us.z64")
    oracle = BootOracle(state)
    patches = return_address_patches(state)
    oracle.invoke("RevealReturnAddresses")
    for patch in patches:
        assert oracle.u32(patch["vram"]) == patch["after"]
    expected_code = bytearray(state["image"])
    for patch in patches:
        struct.pack_into(">I", expected_code, physical(patch["vram"]), patch["after"])
    code_start = physical(state["symbols"]["__CODE_SECTION_START"])
    code_end = physical(state["symbols"]["__DATA_SECTION_START"])
    assert oracle.memory()[code_start:code_end] == expected_code[code_start:code_end]
    print("PASS: original return-address patches:", len(patches), flush=True)
    for name in ("mmInit", "runlinkInitialise"):
        oracle.invoke(name)
        print("PASS:", name, flush=True)
    print("overlay_count:", oracle.u32(state["symbols"]["overlayCount"]), flush=True)
    for number in (19, 6, 32, 44):
        result = oracle.invoke("runlinkDownloadCode", number)
        print("load:", number, "result:", result[2], "base:", hex(oracle.invoke("runlinkIsModuleLoaded", number)[2]), flush=True)
    print("platform_calls:", len(oracle.events), flush=True)
