#!/usr/bin/env python3
"""Differentially execute original MIPS64 and recompiled host functions.

Unicorn is a test oracle, not a runtime dependency of the proposed port. The
original JFG linker applies overlay relocations in the oracle; this deliberately
does not reuse the exporter's address calculations as the expected result.
"""
from __future__ import annotations

import argparse
import array
import ctypes
import hashlib
import json
import platform
import random
import struct
import sys
from pathlib import Path

from unicorn import Uc, UcError, UC_ARCH_MIPS, UC_MODE_BIG_ENDIAN, UC_MODE_MIPS64, UC_HOOK_CODE
from unicorn import mips_const

ROOT = Path(__file__).resolve().parents[2]
RAM_SIZE = 0x800000
STACK = 0x807F0000
RETURN = 0x807FF000
TABLE = 0x80600000
ROM_TABLE = 0x80610000
RELOC_SCRATCH = 0x80680000
OBJECT = 0x80700000
GAME = 0x80710000
OVERLAY_BASES = (0x80200000, 0x803FF000, 0x80508000)
REGISTERS = [getattr(mips_const, f"UC_MIPS_REG_{i}") for i in range(32)]
MASK64 = (1 << 64) - 1


def sx32(value):
    value &= 0xFFFFFFFF
    return value | 0xFFFFFFFF00000000 if value & 0x80000000 else value


def physical(address):
    return address & 0x1FFFFFFF


class Oracle:
    def __init__(self, image):
        self.cpu = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_BIG_ENDIAN)
        self.cpu.ctl_set_cpu_model(mips_const.UC_CPU_MIPS64_R4000)
        self.cpu.mem_map(0, RAM_SIZE)
        self.cpu.mem_write(0, bytes(image))

    def write(self, address, data):
        self.cpu.mem_write(physical(address), data)

    def word(self, address, value):
        self.write(address, struct.pack(">I", value & 0xFFFFFFFF))

    def call(self, address, registers, budget=20000):
        for reg, value in zip(REGISTERS, registers):
            self.cpu.reg_write(reg, value)
        try:
            self.cpu.emu_start(sx32(address), sx32(RETURN), count=budget)
        except UcError as error:
            pc = self.cpu.reg_read(mips_const.UC_MIPS_REG_PC)
            raise RuntimeError(f"MIPS oracle failed at 0x{pc:016X}: {error}") from error
        pc = self.cpu.reg_read(mips_const.UC_MIPS_REG_PC)
        if pc != sx32(RETURN):
            raise RuntimeError(f"MIPS instruction budget exhausted at 0x{pc:016X}")
        return [self.cpu.reg_read(reg) & MASK64 for reg in REGISTERS]

    def memory(self):
        return self.cpu.mem_read(0, RAM_SIZE)


def original_overlay_link(oracle, manifest, section, base, rom):
    """Run the game's actual linker, including its cache maintenance routines."""
    symbols = manifest["reference_symbols"]
    number = section["overlay"]
    original_table = rom[manifest["overlay_table"]:manifest["overlay_data_base"]]
    oracle.write(TABLE + 32, original_table)
    oracle.word(TABLE, symbols["__CODE_SECTION_START"])
    oracle.word(TABLE + number * 32, base)
    original_ort = rom[manifest["overlay_rom_table"]:manifest["overlay_table"]]
    oracle.write(ROM_TABLE, original_ort)
    oracle.word(symbols["overlayTable"], TABLE)
    oracle.word(symbols["overlayRomTable"], ROM_TABLE)
    oracle.word(symbols["gRelocTextBase"], base)
    oracle.word(symbols["gRelocDataBase"], base + section["header"]["text_size"])
    validated = {"groups": 0, "pairs": 0, "calls": 0}
    for pair in section["relocation_groups"]:
        table_offset = section["rom"] + section["load_size"]
        if pair["table"] == "secondary":
            table_offset += section["header"]["reloc_table_size"]
        pair_bytes = rom[table_offset + pair["index"] * 8:table_offset + (pair["index"] + pair["consumed"]) * 8]
        oracle.write(RELOC_SCRATCH, pair_bytes)
        regs = [0] * 32
        regs[4], regs[5] = sx32(RELOC_SCRATCH), number
        regs[29], regs[31] = sx32(STACK), sx32(RETURN)
        result = oracle.call(symbols["ProcessRelocationEntry"], regs)
        if result[2] != pair["consumed"]:
            raise AssertionError("Original linker consumed a different relocation count")
        if pair["kind"] == "pair":
            hi = struct.unpack(">I", oracle.cpu.mem_read(physical(base + pair["hi_offset"]), 4))[0]
            lo = struct.unpack(">I", oracle.cpu.mem_read(physical(base + pair["lo_offset"]), 4))[0]
            signed_lo = struct.unpack(">h", struct.pack(">H", lo & 0xFFFF))[0]
            actual = (((hi & 0xFFFF) << 16) + signed_lo) & 0xFFFFFFFF
            validated["pairs"] += 1
        else:
            word = struct.unpack(">I", oracle.cpu.mem_read(physical(base + pair["patch_offset"]), 4))[0]
            actual = ((word & 0x03FFFFFF) << 2) | ((base + pair["patch_offset"] + 4) & 0xF0000000)
            validated["calls"] += 1
        target = manifest["sections"][pair["target_section"]]
        if target["index"] == section["index"]:
            target_base = base
        elif not target["overlay"]:
            target_base = target["vram"]
        else:
            raise AssertionError("A foreign overlay needs an explicit load scenario")
        expected = (target_base + pair["target_offset"]) & 0xFFFFFFFF
        if actual != expected:
            raise AssertionError(f"Converted relocation disagrees with JFG's linker: {actual:08X} != {expected:08X}")
        validated["groups"] += 1
    return validated


def load_library(path):
    library = ctypes.CDLL(str(path.resolve()))
    library.jfg_poc_function_count.restype = ctypes.c_uint32
    library.jfg_poc_set_section.argtypes = (ctypes.c_uint32, ctypes.c_uint32)
    library.jfg_poc_set_section.restype = ctypes.c_int
    library.jfg_poc_run.argtypes = (ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                    ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_uint64))
    library.jfg_poc_run.restype = ctypes.c_int
    library.jfg_poc_last_call_count.restype = ctypes.c_uint32
    library.jfg_poc_last_call_target.restype = ctypes.c_uint32
    library.jfg_poc_last_return_address.restype = ctypes.c_uint64
    library.jfg_poc_address.argtypes = (ctypes.c_char_p,)
    library.jfg_poc_address.restype = ctypes.c_uint32
    library.jfg_poc_load_section.argtypes = (ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8),
                                           ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t)
    library.jfg_poc_load_section.restype = ctypes.c_int
    library.jfg_poc_unload_section.argtypes = (ctypes.c_uint32,)
    library.jfg_poc_unload_section.restype = ctypes.c_int
    return library


def native_call(library, address, memory, registers, expected_status=0):
    words = array.array("I")
    words.frombytes(memory)
    if words.itemsize != 4 or sys.byteorder != "little":
        raise RuntimeError("This proof requires a little-endian x86-64 host")
    words.byteswap()
    rdram = (ctypes.c_uint8 * RAM_SIZE).from_buffer(words)
    regs_in = (ctypes.c_uint64 * 32)(*registers)
    regs_out = (ctypes.c_uint64 * 32)()
    status = library.jfg_poc_run(address, rdram, RAM_SIZE, regs_in, regs_out)
    if status != expected_status:
        raise RuntimeError(f"Native status at {address:08X}: {status}, expected {expected_status}")
    words.byteswap()
    return list(regs_out), words.tobytes()


def check_loader(library, manifest, rom):
    """Check ROM copying, BSS zeroing, overlap rejection and stale addresses."""
    sections = manifest["sections"]
    for section in sections:
        assert library.jfg_poc_unload_section(section["index"]) == 0
    expected = bytearray([0xA5]) * RAM_SIZE
    words = array.array("I")
    words.frombytes(expected)
    memory = (ctypes.c_uint8 * RAM_SIZE).from_buffer(words)
    rom_buffer = (ctypes.c_uint8 * len(rom)).from_buffer_copy(rom)

    def snapshot():
        copy = array.array("I", words)
        copy.byteswap()
        return copy.tobytes()

    def load(section, base):
        status = library.jfg_poc_load_section(section["index"], base, memory, RAM_SIZE, rom_buffer, len(rom))
        if status:
            raise AssertionError(f"Loader rejected section {section['name']}: {status}")
        offset = physical(base)
        expected[offset:offset + section["load_size"]] = rom[section["rom"]:section["rom"] + section["load_size"]]
        expected[offset + section["load_size"]:offset + section["memory_size"]] = bytes(section["memory_size"] - section["load_size"])
        if snapshot() != expected:
            raise AssertionError("Loaded ROM bytes or zeroed BSS differ from the section layout")

    main = next(s for s in sections if not s["overlay"])
    load(main, main["vram"])
    rounds = 0
    for section in sections:
        if not section["overlay"]:
            continue
        for base in OVERLAY_BASES:
            load(section, base)
            function = section["functions"][0]
            if library.jfg_poc_address(function["name"].encode()) != base + function["offset"]:
                raise AssertionError("Loaded function address is incorrect")
            other = next(s for s in sections if s["overlay"] and s["index"] != section["index"])
            before = snapshot()
            status = library.jfg_poc_load_section(other["index"], base, memory, RAM_SIZE, rom_buffer, len(rom))
            if status != -5 or snapshot() != before:
                raise AssertionError("Overlapping load was not rejected without RAM changes")
            assert library.jfg_poc_unload_section(section["index"]) == 0
            if library.jfg_poc_address(function["name"].encode()) != 0:
                raise AssertionError("Stale function address remains registered")
            for offset in range(physical(base) + section["load_size"], physical(base) + section["memory_size"]):
                memory[offset ^ 3] = 0x5A
                expected[offset] = 0x5A
            load(section, base)
            assert library.jfg_poc_unload_section(section["index"]) == 0
            rounds += 1
    nonempty_bss = sum(s["overlay"] != 0 and s["memory_size"] > s["load_size"] for s in sections) * len(OVERLAY_BASES)
    return {"load_unload_reload_cycles": rounds, "overlap_rejections": rounds,
            "nonempty_bss_reset_checks": nonempty_bss,
            "empty_bss_boundary_checks": rounds - nonempty_bss, "stale_address_checks": rounds}


def run(manifest_path, library_path, report_path, cases_per_function):
    if platform.machine().lower() not in {"x86_64", "amd64"} or ctypes.sizeof(ctypes.c_void_p) != 8:
        raise RuntimeError("Run this proof with an x86-64 Python and native library")
    manifest = json.loads(manifest_path.read_text())
    rom = Path(manifest["rom_path"]).read_bytes()
    if hashlib.sha1(rom).hexdigest() != manifest["rom_sha1"]:
        raise ValueError("ROM changed after preparing the proof")
    library = load_library(library_path)
    if library.jfg_poc_function_count() != len(manifest["functions"]):
        raise AssertionError("Library and metadata disagree on the function count")
    sections = manifest["sections"]
    main = next(s for s in sections if not s["overlay"])
    base_image = bytearray(RAM_SIZE)
    start = physical(main["vram"])
    base_image[start:start + main["load_size"]] = rom[main["rom"]:main["rom"] + main["load_size"]]
    if library.jfg_poc_set_section(main["index"], main["vram"]):
        raise AssertionError("Cannot register the main section")
    data_begin = physical(manifest["reference_symbols"]["__DATA_SECTION_START"])
    data_end = physical(manifest["reference_symbols"]["__BSS_SECTION_END"])
    totals = []
    relocation_checks = {"groups": 0, "pairs": 0, "calls": 0}
    mutation_checks = 0
    calls_executed = 0
    dependency_failures = 0
    conditional_without_dependency = 0
    test_index = 0
    edge_values = (0, 1, 2, 3, 7, 8, -1, -2, -2147483648, 2147483647, 32767, 32768)
    for function in manifest["functions"]:
        section = sections[function["section"]]
        bases = OVERLAY_BASES if section["overlay"] else (main["vram"],)
        passed = 0
        for base in bases:
            if library.jfg_poc_set_section(section["index"], base):
                raise AssertionError("Cannot register the proof section")
            for case in range(cases_per_function):
                rng = random.Random(0x4A464700 + test_index)
                test_index += 1
                memory = bytearray(base_image)
                memory[data_begin:data_end] = rng.randbytes(data_end - data_begin)
                if section["overlay"]:
                    off = physical(base)
                    memory[off:off + section["load_size"]] = rom[section["rom"]:section["rom"] + section["load_size"]]
                    bss_start = off + section["load_size"]
                    bss_end = off + section["memory_size"]
                    memory[bss_start:bss_end] = rng.randbytes(bss_end - bss_start)
                    # Vary the values read by the selected overlay leaf functions.
                    for pair in section["relocation_groups"]:
                        if pair["kind"] == "pair" and pair["target_section"] == section["index"]:
                            struct.pack_into(">I", memory, off + pair["target_offset"], rng.getrandbits(32))
                is_caller = function["name"].startswith(("func_overlay_18_", "func_overlay_32_"))
                if is_caller:
                    struct.pack_into(">I", memory, physical(manifest["reference_symbols"]["gameplay"]), GAME)
                    memory[physical(OBJECT):physical(OBJECT) + 0x80] = rng.randbytes(0x80)
                    memory[physical(GAME):physical(GAME) + 0x400] = rng.randbytes(0x400)
                    memory[physical(OBJECT) + 1] = case % 8
                    memory[physical(OBJECT) + 0x32] = case if case < 12 else rng.randrange(256)
                oracle = Oracle(memory)
                if section["overlay"]:
                    checked = original_overlay_link(oracle, manifest, section, base, rom)
                    for key in relocation_checks:
                        relocation_checks[key] += checked[key]
                    memory = oracle.memory()
                registers = [sx32(rng.getrandbits(32)) for _ in range(32)]
                registers[0] = 0
                registers[4] = sx32(edge_values[case] if case < len(edge_values) else rng.getrandbits(32))
                if is_caller:
                    registers[4] = sx32(OBJECT)
                registers[29], registers[31] = sx32(STACK), sx32(RETURN)
                address = base + function["offset"]
                call_trace = []
                if is_caller:
                    target = next(f for f in manifest["functions"] if f["name"] == "mainGetGame")["vram"]
                    def observe_call(cpu, pc, size, user_data):
                        call_trace.append((pc & 0xFFFFFFFF, cpu.reg_read(mips_const.UC_MIPS_REG_RA) & MASK64))
                    oracle.cpu.hook_add(UC_HOOK_CODE, observe_call, begin=sx32(target), end=sx32(target))
                expected_regs = oracle.call(address, registers)
                expected_memory = oracle.memory()
                actual_regs, actual_memory = native_call(library, address, memory, registers)
                count = library.jfg_poc_last_call_count()
                if count != len(call_trace):
                    raise AssertionError("Native call count differs from the original MIPS execution")
                if call_trace and call_trace[-1] != (library.jfg_poc_last_call_target(), library.jfg_poc_last_return_address()):
                    raise AssertionError("Native call target or relocated return address differs from MIPS")
                calls_executed += count
                if actual_regs != expected_regs:
                    diffs = [(i, hex(a), hex(b)) for i, (a, b) in enumerate(zip(actual_regs, expected_regs)) if a != b]
                    raise AssertionError(f'{function["name"]} case {case} base {base:08X}: register differences {diffs}')
                if actual_memory != expected_memory:
                    offset = next(i for i, (a, b) in enumerate(zip(actual_memory, expected_memory)) if a != b)
                    raise AssertionError(f'{function["name"]} case {case}: RAM differs at {offset:08X}')
                mutation_checks += actual_memory != memory
                passed += 1
            # Fault injection: a required main-module dependency is absent.
            if is_caller or function["name"] == "frontSetInstrumentsHide":
                fault_memory = bytearray(memory)
                if function["name"].startswith("func_overlay_18_"):
                    fault_memory[physical(OBJECT) + 1] = 3
                assert library.jfg_poc_unload_section(main["index"]) == 0
                native_call(library, address, fault_memory, registers, expected_status=-3)
                dependency_failures += 1
                if function["name"].startswith("func_overlay_18_"):
                    fault_memory[physical(OBJECT) + 1] = 0
                    native_call(library, address, fault_memory, registers)
                    if library.jfg_poc_last_call_count() != 0:
                        raise AssertionError("Conditional no-call path invoked a missing dependency")
                    conditional_without_dependency += 1
                assert library.jfg_poc_set_section(main["index"], main["vram"]) == 0
            if section["overlay"]:
                if library.jfg_poc_set_section(section["index"], 0):
                    raise AssertionError("Cannot unregister overlay")
                buffer = (ctypes.c_uint8 * RAM_SIZE)()
                regs = (ctypes.c_uint64 * 32)()
                status = library.jfg_poc_run(base + function["offset"], buffer, RAM_SIZE, regs, regs)
                if status != -2:
                    raise AssertionError("Unloaded overlay remained callable")
        totals.append({"function": function["name"], "overlay": section["overlay"],
                       "cases": passed, "load_bases": [f"0x{base:08X}" for base in bases]})
        print(f'PASS {function["name"]}: {passed} cases', flush=True)
    if not mutation_checks:
        raise AssertionError("No memory-mutating test was exercised")
    if not calls_executed:
        raise AssertionError("No cross-section game call was exercised")
    loader_results = check_loader(library, manifest, rom)
    report = {
        "status": "passed", "host_os": platform.system(), "host_arch": platform.machine(),
        "target": "Windows x64 / NVIDIA; CPU proof has no graphics backend",
        "rom_sha1": manifest["rom_sha1"], "oracle": "Unicorn 2.1.4, MIPS64 R4000, big-endian",
        "functions": totals, "total_cases": sum(t["cases"] for t in totals),
        "original_linker_groups_executed": relocation_checks["groups"],
        "original_linker_pairs_executed": relocation_checks["pairs"],
        "original_linker_call_relocations_executed": relocation_checks["calls"],
        "native_cross_section_calls": calls_executed,
        "call_targets_and_return_addresses_compared": calls_executed,
        "missing_dependency_checks": dependency_failures,
        "conditional_paths_without_dependency": conditional_without_dependency,
        "loader_checks": loader_results,
        "memory_mutating_cases": mutation_checks,
        "comparison": "All 32 general registers and all 8 MiB of guest RAM after every call",
        "limits": manifest["limits"] + ["No FPU, exceptions or whole-console timing validation", "Windows/GPU execution requires its own test run"],
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f'PASS: {report["total_cases"]} differential cases; {relocation_checks["groups"]} original-linker groups; {calls_executed} native calls.', flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/port-recomp/proof/manifest.json")
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--report", type=Path, default=ROOT / "build/port-recomp/report.json")
    parser.add_argument("--cases", type=int, default=24)
    args = parser.parse_args()
    if args.cases < 12:
        parser.error("Use at least 12 cases to include boundary inputs")
    run(args.manifest, args.library, args.report, args.cases)


if __name__ == "__main__":
    main()
