#!/usr/bin/env python3
"""Compare original manager creation, including AI MMIO, with native execution."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_MEM_WRITE

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/bootstrap"))
from verify_bootstrap import BootstrapOracle, masked, HOST, SLOTS, unique_symbol, sx32, RETURN
from checks_manager import ManagerSession, require, load_native_library


class ManagerOracle(BootstrapOracle):
    def __init__(self, *args):
        super().__init__(*args, boundary_name="n_alCSPNew", native_imports=("osAiSetFrequency",))
        self.cpu.mem_map(0x04500000, 0x1000)
        self.audio_writes = []
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.observe_ai, begin=0x04500000, end=0x04500FFF)

    def observe_ai(self, cpu, access, address, size, value, data):
        require(size == 4 and address in (0x04500010, 0x04500014, 0x04500008), "Unexpected AI register write")
        self.audio_writes.append([address, value & 0xFFFFFFFF])


def run(elf_path, rom_path, manifest_path, library_path, report_path):
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    with elf_path.open("rb") as file:
        syms = ELFFile(file).get_section_by_name(".symtab")
        symbols = {name: unique_symbol(syms, name)["st_value"] for name in
                   ("__osDisableInt", "__osRestoreInt", "TrapDanglingJump", "osCreateMesgQueue", "mainInitGame")}
    lib = load_native_library(library_path)
    cases = []
    for tv in (1, 0, 2):
        session = ManagerSession(lib, rom, manifest, tv, dirty_heap=True)
        try:
            oracle = ManagerOracle(session.native.snapshot(), rom, manifest, symbols)
            registers = [0] * 32
            registers[29], registers[31] = sx32(0x80780000 - 0x10), sx32(RETURN)
            oracle.call(symbols["mainInitGame"], registers, budget=15000000)
            boundary = session.bootstrap()
            require(oracle.boundary == {"target": int(boundary["target"], 16), "call_site": int(boundary["call_site"], 16)},
                    "Original manager boundary differs")
            ai = session.ai()
            native_ai_writes = [[ai["write0"], ai["dac"]], [ai["write1"], ai["bitrate"]], [ai["write2"], ai["control"]]]
            require(oracle.audio_writes == native_ai_writes and ai["writes"] == 3, "Original AI configuration writes differ")
            actual_registers = session.poll(SLOTS)[2]
            compared = (0, 4, 5, 6, 7, *range(16, 24), 28, 29, 30, 31)
            require(all(actual_registers[i] == oracle.boundary_registers[i] for i in compared), "Manager preserved GPRs differ")
            transfers = [[e[1], e[2], e[3]] for e in session.native.events() if e[0] == 1]
            require(transfers == oracle.transfers, "Original manager ROM transfers differ")
            require(oracle.traps == [0x80044EF8, int(boundary["overlay36"], 16) + 0x14], "Original lazy-load path differs")
            regions = [(HOST, 0x100), (SLOTS, 0x200), (session.symbols["sc"] + 0xB0, 0x1B0),
                       (session.symbols["Time"] - 0x400, 0x420), (0x80780000 - 0x10 - 0x2000, 0x2040),
                       (session.symbols["D_800F17C0_B9770"], 0x1B0)]
            regions += [(q, 8) for q in sorted(oracle.queues)]
            expected, actual = masked(oracle.memory(), regions), masked(session.native.snapshot(), regions)
            if expected != actual:
                offset = next(i for i, (a, b) in enumerate(zip(expected, actual)) if a != b)
                raise AssertionError(f"Manager RAM differs at {offset:08X}: {expected[offset:offset+16].hex()} != {actual[offset:offset+16].hex()}")
            cases.append({"name": f"original_manager_tv_{tv}", "status": "passed", "boundary": boundary,
                          "compared_gprs": list(compared), "rom_transfers": len(transfers), "ai_writes": native_ai_writes,
                          "masked_regions": [[f"0x{a:08X}", n] for a,n in regions],
                          "ram_sha256": hashlib.sha256(actual).hexdigest()})
            print(f"PASS MIPS audio manager TV {tv}", flush=True)
        finally:
            joined = session.close()
        cases[-1]["threads_joined"] = joined
    report = {"status": "passed", "cases": cases, "limits": [
        "Original osAiSetFrequency executes with observed MMIO; other explicit platform hooks retained",
        "17 GPRs and RAM compared with kernel/wait-list/call-stack exclusions; no general FPU/FCSR proof",
        "Audio thread is created but not started; stops before n_alCSPNew"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.elf, a.rom, a.manifest, a.library, a.report)
