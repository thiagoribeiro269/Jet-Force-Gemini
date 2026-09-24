#!/usr/bin/env python3
"""Compare the headless AI frequency contract with JFG US MIPS and MMIO writes."""
from __future__ import annotations

import argparse
import ctypes
import json
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_BIG_ENDIAN, UC_MODE_MIPS64, UC_HOOK_MEM_WRITE
from unicorn import mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/threads"))
from checks import Session, configure, require, require_success, load_native_library  # noqa: E402

FUNCTION = 0x80096A50
CLOCK_SYMBOL = 0x800A9F18
RETURN = 0x807FF000
AI_WRITES = (0x04500010, 0x04500014, 0x04500008)
CLOCKS = (("NTSC", 48681812), ("PAL", 49656530), ("MPAL", 48628316))
FREQUENCIES = (22020, 44100, 369000, 400000, 2882, 2881, 2800, 1, 0x80000000, 0xFFFFFFFF)


def signed64(value: int) -> int:
    value &= 0xFFFFFFFF
    return value | 0xFFFFFFFF00000000 if value & 0x80000000 else value


def reference(image: bytes, clock: int, frequency: int) -> tuple[int, list[tuple[int, int]]]:
    cpu = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_BIG_ENDIAN)
    cpu.ctl_set_cpu_model(mips_const.UC_CPU_MIPS64_R4000)
    cpu.mem_map(0, 0x1000000)
    cpu.mem_map(0x04500000, 0x1000)
    cpu.mem_write(0x400, image)
    cpu.mem_write(CLOCK_SYMBOL & 0x1FFFFFFF, struct.pack(">I", clock))
    cpu.reg_write(mips_const.UC_MIPS_REG_4, signed64(frequency))
    cpu.reg_write(mips_const.UC_MIPS_REG_SP, signed64(0x800FF000))
    cpu.reg_write(mips_const.UC_MIPS_REG_RA, signed64(RETURN))
    writes: list[tuple[int, int]] = []

    def observe(_cpu, _access, address, size, value, _user):
        require(size == 4 and address in AI_WRITES, f"Unexpected original MMIO write {address:08X}/{size}")
        writes.append((address, value & 0xFFFFFFFF))

    cpu.hook_add(UC_HOOK_MEM_WRITE, observe)
    cpu.emu_start(signed64(FUNCTION), signed64(RETURN), count=1000)
    require(cpu.reg_read(mips_const.UC_MIPS_REG_PC) == signed64(RETURN), "Original MIPS did not return")
    return cpu.reg_read(mips_const.UC_MIPS_REG_2) & 0xFFFFFFFF, writes


def run(elf_path: Path, library_path: Path, manifest_path: Path, rom_path: Path, report_path: Path) -> dict:
    with elf_path.open("rb") as source:
        elf = ELFFile(source)
        image = elf.get_section_by_name(".main").data()
        symbols = elf.get_section_by_name(".symtab")
        require(next(s["st_value"] for s in symbols.iter_symbols() if s.name == "osAiSetFrequency") == FUNCTION,
                "Unexpected US osAiSetFrequency address")
        require(next(s["st_value"] for s in symbols.iter_symbols() if s.name == "osViClock") == CLOCK_SYMBOL,
                "Unexpected US osViClock address")

    library = load_native_library(library_path)
    configure(library)
    byteptr = ctypes.POINTER(ctypes.c_uint8)
    library.jfg_ai_begin.argtypes = (byteptr, ctypes.c_uint32)
    library.jfg_ai_begin.restype = ctypes.c_int
    library.jfg_ai_end.argtypes = (byteptr,)
    library.jfg_ai_end.restype = ctypes.c_int
    library.jfg_ai_state.argtypes = (ctypes.POINTER(ctypes.c_uint32), ctypes.c_size_t)
    library.jfg_ai_state.restype = ctypes.c_int
    library.jfg_ai_frequency.argtypes = (byteptr, ctypes.c_uint32, ctypes.POINTER(ctypes.c_int32))
    library.jfg_ai_frequency.restype = ctypes.c_int
    manifest = json.loads(manifest_path.read_text())
    require(manifest["reference_symbols"].get("osViClock") == CLOCK_SYMBOL,
            "Profile must retain the exact osViClock symbol")
    require(any(f["name"] == "osAiSetFrequency" and f["host_import"] for f in manifest["functions"]),
            "Profile must import osAiSetFrequency")
    session = Session(library, rom_path.read_bytes(), manifest, initialize_game=False)
    cases = []
    try:
        require_success(library.jfg_ai_begin(session.native.memory, CLOCK_SYMBOL), "Bind AI state")
        fields = (ctypes.c_uint32 * 12)()
        previous_writes = 0
        rejected = 0
        for tv, clock in CLOCKS:
            session.set(CLOCK_SYMBOL, clock)
            for frequency in FREQUENCIES:
                expected_result, expected_writes = reference(image, clock, frequency)
                before = tuple(fields)
                result = session.call("osAiSetFrequency", frequency)[2] & 0xFFFFFFFF
                require_success(library.jfg_ai_state(fields, 12), "Read AI state")
                actual_writes = ([] if fields[7] == previous_writes else
                                 [(fields[9], fields[4]), (fields[10], fields[5]), (fields[11], fields[6])])
                require(result == expected_result, f"{tv}/{frequency}: return {result} != {expected_result}")
                require(actual_writes == expected_writes,
                        f"{tv}/{frequency}: AI writes {actual_writes} != {expected_writes}")
                require(fields[1] == clock and fields[2] == frequency and fields[3] == result,
                        f"{tv}/{frequency}: state clock/request/result mismatch")
                require(fields[7] == previous_writes + len(expected_writes),
                        f"{tv}/{frequency}: cumulative write count mismatch")
                if not expected_writes:
                    require(tuple(fields[i] for i in (4, 5, 6, 9, 10, 11)) ==
                            tuple(before[i] for i in (4, 5, 6, 9, 10, 11)),
                            f"{tv}/{frequency}: rejected request changed AI registers/write order")
                if expected_result == 0xFFFFFFFF:
                    rejected += 1
                require(fields[8] == rejected, f"{tv}/{frequency}: rejection count mismatch")
                previous_writes = fields[7]
                cases.append({"tv": tv, "requested": frequency, "returned": result if result < 0x80000000 else result - 0x100000000,
                              "writes": [[f"0x{address:08X}", f"0x{value:08X}"] for address, value in expected_writes]})
        # The adapter excludes zero; all other u32 values follow the ROM.
        output = ctypes.c_int32()
        before = tuple(fields)
        require(library.jfg_ai_frequency(session.native.memory, 0, ctypes.byref(output)) == -1,
                "Zero adapter input accepted")
        session.call("osAiSetFrequency", 0, expected_status=-14)
        require_success(library.jfg_ai_state(fields, 12), "Read unchanged AI state")
        require(tuple(fields) == before, "Zero input changed AI state")
        require_success(library.jfg_ai_end(session.native.memory), "Unbind AI state")
    finally:
        session.close()
    report = {"status": "passed", "cases": cases, "limits": ["CPU return and AI register contract only; no audio samples"]}
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(cases)} original MIPS/host frequency comparisons; no sample output")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--report", type=Path, default=ROOT / "build/port-audio-manager/frequency-report.json")
    args = parser.parse_args()
    run(args.elf, args.library, args.manifest, args.rom, args.report)
