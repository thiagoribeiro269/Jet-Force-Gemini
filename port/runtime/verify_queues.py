#!/usr/bin/env python3
"""Compare native runtime message queues against the original libultra MIPS."""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE, mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/boot"))
from reference import profile, unique_symbol, Oracle, sx32, RETURN
from queue_checks import NativeQueues, initial_image, observable_memory, physical, QUEUE, BUFFER, OUTPUT, STACK, RESULT_SENTINEL

FUNCTIONS = ("osCreateMesgQueue", "osSendMesg", "osJamMesg", "osRecvMesg")


class QueueOracle(Oracle):
    def __init__(self, image, symbols):
        super().__init__(image)
        self.symbols = symbols
        for name in ("__osDisableInt", "__osRestoreInt"):
            address = sx32(symbols[name])
            self.cpu.hook_add(UC_HOOK_CODE, self.interrupt_contract, name, begin=address, end=address)

    def interrupt_contract(self, cpu, pc, size, name):
        # No asynchronous interrupts exist in this serialized API comparison.
        if name == "__osDisableInt":
            cpu.reg_write(mips_const.UC_MIPS_REG_2, 1)
        cpu.reg_write(mips_const.UC_MIPS_REG_PC, cpu.reg_read(mips_const.UC_MIPS_REG_RA))

    def invoke(self, operation, queue, value, argument):
        registers = [0] * 32
        registers[4:7] = [sx32(v) for v in (queue, value, argument)]
        registers[29], registers[31] = sx32(STACK), sx32(RETURN)
        result = self.call(self.symbols[FUNCTIONS[operation]], registers)
        return ctypes.c_int32(result[2] & 0xFFFFFFFF).value if operation else 0


def run(elf_path, rom_path, library_path, output):
    state = profile(elf_path, rom_path)
    with elf_path.open("rb") as file:
        elf = ELFFile(file)
        symtab = elf.get_section_by_name(".symtab")
        symbols = {name: unique_symbol(symtab, name)["st_value"] for name in
                   (*FUNCTIONS, "__osDisableInt", "__osRestoreInt")}
        metadata = {"vram": state["main_vram"], "rom": state["main_rom"],
                    "size": elf.get_section_by_name(".main")["sh_size"]}
    image = initial_image(state["rom"], metadata)
    library = ctypes.CDLL(str(library_path.resolve()))
    scenarios = []
    for capacity in (1, 2, 4, 16):
        native = NativeQueues(library, image)
        oracle = QueueOracle(image, symbols)
        steps = []

        def check(operation, value, argument=0, queue=QUEUE, status=0):
            before = native.snapshot()
            actual_status, actual_result = native.call(operation, queue, value, argument)
            if actual_status != status:
                raise AssertionError(f"Unexpected status {actual_status}: {(operation, queue, value, argument)}")
            step = {"call": [operation, queue, value, argument], "status": status, "result": actual_result}
            if status:
                if native.snapshot() != before or actual_result != RESULT_SENTINEL:
                    raise AssertionError("Rejected operation changed RAM or the result")
            else:
                expected_result = oracle.invoke(operation, queue, value, argument)
                if actual_result != expected_result:
                    raise AssertionError(f"Guest return differs: {actual_result} != {expected_result}")
                expected_ram = observable_memory(oracle.memory())
                actual_ram = observable_memory(native.snapshot())
                if actual_ram != expected_ram:
                    address = next(i for i, (a,b) in enumerate(zip(actual_ram, expected_ram)) if a != b)
                    raise AssertionError(f"Queue RAM differs at {address:08X}")
                step["ram_sha256"] = hashlib.sha256(expected_ram).hexdigest()
            steps.append(step)

        check(0, BUFFER, capacity)
        check(3, OUTPUT)  # empty nonblocking receive
        check(3, OUTPUT, 1, status=-3)  # suspension is intentionally unsupported
        for index in range(capacity):
            check(1, 0x80000000 + index, 1)  # blocking flag, but no suspension needed
        check(1, 42)  # full nonblocking send
        check(2, 43)  # full nonblocking jam
        check(1, 44, 1, status=-3)
        check(2, 45, 1, status=-3)
        check(3, 0)  # receive and discard
        rng = random.Random(0x4A4647 + capacity)
        for _ in range(128):
            operation = rng.choice((1, 2, 3))
            value = (OUTPUT if rng.randrange(4) else 0) if operation == 3 else rng.getrandbits(32)
            count = struct.unpack_from(">i", oracle.memory(), physical(QUEUE) + 8)[0]
            flags = rng.randrange(2)
            blocked = flags == 1 and (count == 0 if operation == 3 else count == capacity)
            check(operation, value, flags, status=-3 if blocked else 0)
        check(0, BUFFER, 0, status=-1)
        check(0, QUEUE, 4, status=-1)
        check(1, 0, 9, status=-2)
        check(99, 0, status=-2)
        check(1, 0, queue=0x807FFFF8, status=-1)
        check(3, 0x80800000, status=-1)
        scenarios.append({"capacity": capacity, "steps": steps})
        print(f"PASS original MIPS/runtime queue: capacity {capacity}, {len(steps)} checks", flush=True)
    fixtures = {"format": 1, "rom_sha1": hashlib.sha1(state["rom"]).hexdigest(), "main_image": metadata,
                "comparison": "API return and all 8 MiB RAM except the 8-byte queue wait-list fields and 576-byte guest call stack scratch",
                "reference": "Original libultra MIPS functions from verified JFG US; serialized interrupt contract",
                "scenarios": scenarios}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(fixtures, indent=2) + "\n")
    print(f"PASS: {sum(len(s['steps']) for s in scenarios)} runtime queue checks; fixtures: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-runtime/queue-fixtures.json")
    args = parser.parse_args()
    run(args.elf, args.rom, args.library, args.out)
