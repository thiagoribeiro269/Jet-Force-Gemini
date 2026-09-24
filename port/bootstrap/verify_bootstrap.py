#!/usr/bin/env python3
"""Compare original lazy linking and the audio entry boundary against MIPS."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE, mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/init"))
from verify_init import InitOracle, masked, HOST, SLOTS, unique_symbol, sx32, RETURN, physical
from checks_bootstrap import BootstrapSession, configure_bootstrap, require, load_native_library


class BootstrapOracle(InitOracle):
    def __init__(self, *args, boundary_name="amInit", native_imports=()):
        super().__init__(*args, trap_boundary=False, native_imports=native_imports)
        self.boundary_name = boundary_name
        self.traps = []
        self.boundary_registers = None
        trap = sx32(self.symbols["TrapDanglingJump"])
        self.cpu.hook_add(UC_HOOK_CODE, self.observe_trap, begin=trap, end=trap)
        if boundary_name != "amInit":
            function = next(f for f in self.manifest["functions"] if f["name"] == boundary_name)
            section = self.manifest["sections"][function["section"]]
            require(section["overlay"] == 0, "Additional oracle boundary must be in main")
            address = sx32(function["vram"])
            self.cpu.hook_add(UC_HOOK_CODE, self.stop_at_audio, begin=address, end=address)

    def observe_trap(self, cpu, pc, size, data):
        self.traps.append((cpu.reg_read(mips_const.UC_MIPS_REG_RA) - 8) & 0xFFFFFFFF)

    def stop_at_audio(self, cpu, pc, size, data):
        self.boundary = {"target": pc & 0xFFFFFFFF,
                         "call_site": (cpu.reg_read(mips_const.UC_MIPS_REG_RA) - 8) & 0xFFFFFFFF}
        self.boundary_registers = [cpu.reg_read(getattr(mips_const, f"UC_MIPS_REG_{i}")) for i in range(32)]
        cpu.reg_write(mips_const.UC_MIPS_REG_PC, sx32(RETURN))

    def platform(self, cpu, pc, size, name):
        if name == "osPiStartDma" and self.boundary_name == "amInit":
            source = cpu.reg_read(mips_const.UC_MIPS_REG_7) & 0xFFFFFFFF
            sp = cpu.reg_read(mips_const.UC_MIPS_REG_SP) & 0xFFFFFFFF
            section = next(s for s in self.manifest["sections"] if s["overlay"] == 25)
            if source == section["rom"]:
                entry = sx32(self.u32(sp + 0x10) + 0x308)
                self.cpu.hook_add(UC_HOOK_CODE, self.stop_at_audio, begin=entry, end=entry)
        super().platform(cpu, pc, size, name)


def run(elf_path, rom_path, manifest_path, library_path, report_path, *,
        session_class=BootstrapSession, boundary_name="amInit"):
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    with elf_path.open("rb") as file:
        syms = ELFFile(file).get_section_by_name(".symtab")
        symbols = {name: unique_symbol(syms, name)["st_value"] for name in
                   ("__osDisableInt", "__osRestoreInt", "TrapDanglingJump", "osCreateMesgQueue", "mainInitGame")}
    lib = load_native_library(library_path)
    configure_bootstrap(lib)
    cases = []
    for tv in (1, 0, 2):
        session = session_class(lib, rom, manifest, tv, dirty_heap=True)
        try:
            oracle = BootstrapOracle(session.native.snapshot(), rom, manifest, symbols, boundary_name=boundary_name)
            registers = [0] * 32
            registers[29], registers[31] = sx32(0x80780000 - 0x10), sx32(RETURN)
            oracle.call(symbols["mainInitGame"], registers, budget=12000000)
            boundary = session.bootstrap()
            require(oracle.boundary == {"target": int(boundary["target"], 16), "call_site": int(boundary["call_site"], 16)},
                    "MIPS audio boundary differs")
            require(oracle.traps == [0x80044EF8, int(boundary["overlay36"], 16) + 0x14], "Original lazy-loader path differs")
            actual_registers = session.poll(SLOTS)[2]
            # Platform contracts do not model libultra scratch registers.
            # Trap itself restores arguments, return address and stack.
            compared = (0, 4, 5, 6, 7, *range(16, 24), 28, 29, 30, 31)
            require(all(actual_registers[i] == oracle.boundary_registers[i] for i in compared),
                    "Bootstrap preserved registers differ from MIPS")
            transfers = [[e[1], e[2], e[3]] for e in session.native.events() if e[0] == 1]
            require(transfers == oracle.transfers, "Dynamic overlay ROM transfers differ")
            regions = [(HOST, 0x100), (SLOTS, 0x200), (session.symbols["sc"] + 0xB0, 0x1B0),
                       (session.symbols["Time"] - 0x400, 0x420), (0x80780000 - 0x10 - 0x2000, 0x2040)]
            regions += [(q, 8) for q in sorted(oracle.queues)]
            expected, actual = masked(oracle.memory(), regions), masked(session.native.snapshot(), regions)
            if expected != actual:
                offset = next(i for i, (a, b) in enumerate(zip(expected, actual)) if a != b)
                raise AssertionError(f"Bootstrap RAM differs at {offset:08X}: {expected[offset:offset+16].hex()} != {actual[offset:offset+16].hex()}")
            cases.append({"name": f"original_bootstrap_tv_{tv}", "status": "passed", "boundary": boundary,
                          "compared_gprs": list(compared), "rom_transfers": len(transfers),
                          "masked_regions": [[f"0x{a:08X}", n] for a,n in regions],
                          "ram_sha256": hashlib.sha256(actual).hexdigest()})
            print(f"PASS MIPS dynamic bootstrap TV {tv}", flush=True)
        finally:
            joined = session.close()
        cases[-1]["threads_joined"] = joined
    report = {"status": "passed", "cases": cases, "limits": [
        "Explicit platform hooks; private kernel structures and call stacks excluded from RAM comparison",
        "Only argument/callee-saved/stack/return GPRs compared; FPU registers are not compared",
        "PI completion is immediate in MIPS and owner-pumped natively; no console timing proof",
        f"Stops before executing {boundary_name}"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.elf, a.rom, a.manifest, a.library, a.report)
