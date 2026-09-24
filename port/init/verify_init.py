#!/usr/bin/env python3
"""Compare the initialization prefix and safe decompression with original MIPS."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE, mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/boot"))
from reference import Oracle, sx32, RETURN, unique_symbol
from checks_init import InitSession, configure_init, require, load_native_library, physical
from checks import HOST, SLOTS


class InitOracle(Oracle):
    def __init__(self, image, rom, manifest, symbols, trap_boundary=True):
        super().__init__(image)
        self.rom, self.manifest, self.symbols = rom, manifest, symbols
        self.queues = set()
        self.command_queue = 0
        self.transfers = []
        self.boundary = None
        self.mask = 0x3FFF01
        native_messages = {"osCreateMesgQueue", "osSendMesg", "osJamMesg", "osRecvMesg", "osSetEventMesg"}
        for function in manifest["functions"]:
            if function["host_import"] and function["name"] not in native_messages:
                address = sx32(function["vram"])
                self.cpu.hook_add(UC_HOOK_CODE, self.platform, function["name"], begin=address, end=address)
        for name in ("__osDisableInt", "__osRestoreInt", *(("TrapDanglingJump",) if trap_boundary else ())):
            address = sx32(symbols[name])
            self.cpu.hook_add(UC_HOOK_CODE, self.platform, name, begin=address, end=address)
        address = sx32(symbols["osCreateMesgQueue"])
        self.cpu.hook_add(UC_HOOK_CODE, self.observe_queue, begin=address, end=address)

    def u32(self, address): return struct.unpack(">I", self.cpu.mem_read(physical(address), 4))[0]
    def put(self, address, value): self.write(address, struct.pack(">I", value & 0xFFFFFFFF))
    def observe_queue(self, cpu, pc, size, data): self.queues.add(cpu.reg_read(mips_const.UC_MIPS_REG_4) & 0xFFFFFFFF)

    def enqueue(self, queue, message, high=False):
        count, first, capacity, buffer = [self.u32(queue + n) for n in (8, 12, 16, 20)]
        if count >= capacity: return -1
        if high:
            first = (first + capacity - 1) % capacity
            self.put(queue + 12, first)
        position = first if high else (first + count) % capacity
        self.put(buffer + position * 4, message)
        self.put(queue + 8, count + 1)
        return 0

    def platform(self, cpu, pc, size, name):
        args = [cpu.reg_read(getattr(mips_const, f"UC_MIPS_REG_{i}")) & 0xFFFFFFFF for i in range(4, 8)]
        sp = cpu.reg_read(mips_const.UC_MIPS_REG_SP) & 0xFFFFFFFF
        ra = cpu.reg_read(mips_const.UC_MIPS_REG_RA)
        if name == "TrapDanglingJump":
            self.boundary = {"target": pc & 0xFFFFFFFF, "call_site": (ra - 8) & 0xFFFFFFFF}
            cpu.reg_write(mips_const.UC_MIPS_REG_PC, sx32(RETURN))
            return
        if name == "osPiStartDma":
            mb, priority, direction, source = args
            destination, length, queue = [self.u32(sp + n) for n in (0x10, 0x14, 0x18)]
            require(direction == 0 and source + length <= len(self.rom), "Reference requested unsupported PI transfer")
            status = self.u32(mb) & 0xFF
            self.write(mb, struct.pack(">IIIIII", (11 << 16) | (priority << 8) | status,
                                       queue, destination, source, length, 0))
            require(self.enqueue(self.command_queue, mb, bool(priority)) == 0, "Reference PI command queue full")
            first, capacity = self.u32(self.command_queue + 12), self.u32(self.command_queue + 16)
            self.put(self.command_queue + 8, self.u32(self.command_queue + 8) - 1)
            self.put(self.command_queue + 12, (first + 1) % capacity)
            self.write(destination, self.rom[source:source + length])
            require(self.enqueue(queue, mb) == 0, "Reference completion queue full")
            self.transfers.append([source, destination, length])
            cpu.reg_write(mips_const.UC_MIPS_REG_2, 0)
        elif name == "osCreatePiManager":
            self.command_queue = args[1]
        elif name == "__osDisableInt":
            cpu.reg_write(mips_const.UC_MIPS_REG_2, 1)
        elif name == "osSetIntMask":
            cpu.reg_write(mips_const.UC_MIPS_REG_2, sx32(self.mask))
            self.mask = args[0]
        elif name in ("osGetCount", "osGetTime"):
            cpu.reg_write(mips_const.UC_MIPS_REG_2, 0)
            if name == "osGetTime": cpu.reg_write(mips_const.UC_MIPS_REG_3, 0)
        elif name not in ("osCreateThread", "osStartThread", "osCreateViManager", "osViSetMode", "osViBlack",
                           "osViSetSpecialFeatures", "osViSetEvent", "osInvalDCache", "osInvalICache",
                           "osWritebackDCache", "osWritebackDCacheAll", "__osRestoreInt"):
            raise AssertionError(f"Unproven reference platform path: {name}")
        cpu.reg_write(mips_const.UC_MIPS_REG_PC, ra)


def masked(image, regions):
    data = bytearray(image)
    for address, count in regions:
        data[physical(address):physical(address) + count] = bytes(count)
    return data


def run(elf_path, rom_path, manifest_path, library_path, report_path):
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    with elf_path.open("rb") as file:
        symtab = ELFFile(file).get_section_by_name(".symtab")
        symbols = {name: unique_symbol(symtab, name)["st_value"] for name in
                   ("__osDisableInt", "__osRestoreInt", "TrapDanglingJump", "osCreateMesgQueue", "mainInitGame", "rzipUncompress")}
    library = load_native_library(library_path)
    configure_init(library)
    cases = []
    for tv in (1, 0, 2):
        session = InitSession(library, rom, manifest, tv, dirty_heap=True)
        reference = InitOracle(session.native.snapshot(), rom, manifest, symbols)
        registers = [0] * 32
        registers[29], registers[31] = sx32(0x80780000 - 0x10), sx32(RETURN)
        reference.call(symbols["mainInitGame"], registers, budget=10000000)
        require(reference.boundary == {"target": manifest["init_profile"]["bootstrap_target"],
                                       "call_site": manifest["init_profile"]["bootstrap_call_site"]}, "MIPS did not reach the expected bootstrap boundary")
        session.bootstrap()
        native_transfers = [[e[1], e[2], e[3]] for e in session.native.events() if e[0] == 1]
        require(native_transfers == reference.transfers, "Original and native ROM transfer traces differ")
        regions = [(HOST, 0x100), (SLOTS, 0x200), (session.symbols["sc"] + 0xB0, 0x1B0),
                   (session.symbols["Time"] - 0x400, 0x420), (0x80780000 - 0x10 - 0x2000, 0x2040)]
        regions += [(q, 8) for q in sorted(reference.queues)]
        expected, actual = masked(reference.memory(), regions), masked(session.native.snapshot(), regions)
        if expected != actual:
            offset = next(i for i, (a,b) in enumerate(zip(expected, actual)) if a != b)
            raise AssertionError(f"Initialization RAM differs at {offset:08X}: {expected[offset:offset+16].hex()} != {actual[offset:offset+16].hex()}")
        cases.append({"name": f"main_init_prefix_tv_{tv}", "status": "passed", "rom_transfers": len(native_transfers),
                      "masked_regions": [[f"0x{a:08X}", n] for a,n in regions], "ram_sha256": hashlib.sha256(actual).hexdigest(),
                      "threads_joined": session.close()})
        print(f"PASS MIPS initialization prefix TV {tv}", flush=True)

    session = InitSession(library, rom, manifest)
    session.bootstrap()
    packed = session.run_function("piRomLoad", 19)[2] & 0xFFFFFFFF
    expected_size = int.from_bytes(session.read(packed, 4), "little")
    output = session.call("mmAlloc", expected_size, 0x7F7F7FFF)[2] & 0xFFFFFFFF
    oracle = Oracle(session.native.snapshot())
    index = session.count
    registers = [0] * 32
    registers[4], registers[5] = sx32(packed), sx32(output)
    registers[29], registers[31] = sx32(0x80780000 - index * 0x2000 - 0x10), sx32(RETURN)
    expected_registers = oracle.call(symbols["rzipUncompress"], registers, budget=10000000)
    actual_registers = session.run_function("rzipUncompress", packed, output)
    require(actual_registers == expected_registers, "Decompressor GPRs differ from MIPS")
    regions = [(HOST, 0x100), (SLOTS + index * 0x200, 0x200)]
    require(masked(session.native.snapshot(), regions) == masked(oracle.memory(), regions), "Decompressor RAM differs from original MIPS")
    table = struct.unpack(">76I", rom[session.symbols["__ASSETS_LUT_START"]:session.symbols["__ASSETS_LUT_END"]])
    start, end = [session.symbols["__ASSETS_LUT_END"] + table[i] for i in (20, 21)]
    expected = zlib.decompress(rom[start + 5:end], -15)
    require(session.read(output, len(expected)) == expected, "Decompressor differs from independent zlib")
    cases.append({"name": "original_decompression", "status": "passed", "gprs_compared": 32,
                  "decompressed_bytes": len(expected), "sha256": hashlib.sha256(expected).hexdigest(),
                  "masked_regions": [[f"0x{a:08X}", n] for a,n in regions], "threads_joined": session.close()})
    print("PASS MIPS and zlib decompression", flush=True)
    report = {"status": "passed", "cases": cases,
              "reference": "Original MIPS game bodies with explicit platform hooks; ROM bytes and zlib provide independent data checks",
              "limits": ["Initialization comparison stops before executing TrapDanglingJump",
                         "Kernel thread structures, wait-list fields and initialization call stacks are masked",
                         "PI completion is immediate in the MIPS oracle and owner-pumped in native execution",
                         "No graphics/RSP/RDP hardware or complete game boot"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    p.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    p.add_argument("--manifest", type=Path, default=ROOT / "build/port-init/proof/manifest.json")
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--report", type=Path, default=ROOT / "build/port-init/mips-report.json")
    a = p.parse_args()
    run(a.elf, a.rom, a.manifest, a.library, a.report)
