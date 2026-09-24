#!/usr/bin/env python3
"""Compare new recompiled message consumers with their original MIPS bodies."""
import argparse
import json
from pathlib import Path
import struct
import sys

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/runtime"))
from verify_queues import QueueOracle, unique_symbol
from checks import Session, configure, require, HOST, SLOTS, BUFFER, PAYLOAD
from native import load_native_library, physical, sx32, RETURN


def observable(image, queues):
    image = bytearray(image)
    regions = [(HOST, 0x100), (SLOTS, 0x200), (0x80780000 - 0x10 - 0x200, 0x240)]
    regions += [(q, 8) for q in queues]
    for address, size in regions:
        image[physical(address):physical(address) + size] = bytes(size)
    return image


def run(elf_path, rom_path, manifest_path, library_path, report_path):
    manifest = json.loads(manifest_path.read_text())
    rom = rom_path.read_bytes()
    with elf_path.open("rb") as file:
        elf = ELFFile(file)
        symtab = elf.get_section_by_name(".symtab")
        symbols = {name: unique_symbol(symtab, name)["st_value"] for name in
                   ("__osDisableInt", "__osRestoreInt", "osCreateMesgQueue", "osSendMesg",
                    "rcpWaitDP", "mainResetPressed")}
    library = load_native_library(library_path)
    configure(library)
    cases = []
    for active, blur, refraction in ((0, 0, 0), (1, 0, 0), (1, 1, 0), (1, 1, 1)):
        session = Session(library, rom, manifest)
        queues = [session.symbols[n] for n in ("D_800FF1C8", "D_800FF628", "refractDoneMsgQueue")]
        for name, value in (("D_800A4034", active), ("blurTaskActive", blur),
                            ("refractTaskActive", refraction), ("cloneTaskActive", 0)):
            session.set(session.symbols[name], value)
        session.set(PAYLOAD + 4, 0x81234567)
        oracle = QueueOracle(session.native.snapshot(), symbols)

        def original(name, *arguments):
            registers = [0] * 32
            for index, value in enumerate(arguments, 4): registers[index] = sx32(value)
            registers[29], registers[31] = sx32(0x80780000 - 0x10), sx32(RETURN)
            return oracle.call(symbols[name], registers)

        for index, queue in enumerate(queues):
            session.queue(queue, BUFFER + index * 16)
            original("osCreateMesgQueue", queue, BUFFER + index * 16, 1)
        for queue, enabled in zip(queues, (active, blur, refraction)):
            if enabled:
                session.call("osSendMesg", queue, PAYLOAD, 0)
                original("osSendMesg", queue, PAYLOAD, 0)
        expected = original("rcpWaitDP")
        worker = session.create("rcpWaitDP")
        session.start(worker)
        actual = session.result(worker)
        preserved = (2, *range(16, 24), 28, 29, 30, 31)
        require(all(actual[i] == expected[i] for i in preserved), "Game return or preserved GPR differs from MIPS")
        expected_memory, actual_memory = observable(oracle.memory(), queues), observable(session.native.snapshot(), queues)
        if expected_memory != actual_memory:
            offset = next(i for i, (a,b) in enumerate(zip(expected_memory, actual_memory)) if a != b)
            raise AssertionError(f"Game RAM differs from MIPS at {offset:08X}")
        joined = session.close()
        case = {"function": "rcpWaitDP", "dp_active": active, "blur_active": blur,
                "refraction_active": refraction, "return_v0": actual[2], "threads_joined": joined}
        cases.append(case)
        print("PASS original MIPS rcpWaitDP:", active, blur, refraction, flush=True)
    report = {"status": "passed", "cases": cases,
              "comparison": "Return v0 and callee-saved GPRs; all 8 MiB RAM except host/thread slots, wait-list fields and guest call stack scratch",
              "excluded_ram_bytes": 0x100 + 0x200 + 0x240 + 3 * 8,
              "contract": "Original MIPS libultra consumes prequeued completion messages with serialized interrupt hooks",
              "limits": ["This checks the game body under ready-message conditions, not MIPS thread context switching",
                         "Native blocking/resume and cancellation are tested separately by checks.py",
                         "No GPU or physical DP/RSP execution"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/port-threads/proof/manifest.json")
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--report", type=Path, default=ROOT / "build/port-threads/game-reference-report.json")
    args = parser.parse_args()
    run(args.elf, args.rom, args.manifest, args.library, args.report)
