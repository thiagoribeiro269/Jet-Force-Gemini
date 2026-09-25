#!/usr/bin/env python3
"""Compare the selected asset load with MIPS, separately from bootstrap proof."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "objects"))
from checks_graphics import GraphicsSession
from verify_objects import ObjectsOracle
from verify_start import run as verify_bootstrap
from verify_init import InitOracle, masked, HOST, SLOTS, sx32, RETURN, unique_symbol
from checks_manager import require, load_native_library
from object_checks import check_objects
from graphics_asset_checks import check_graphics_assets


def run(elf_path, rom_path, manifest_path, library_path, report_path):
    # Prove this profile's bootstrap from the original initial image first.
    bootstrap = verify_bootstrap(elf_path, rom_path, manifest_path, library_path,
                                 report_path.with_name("bootstrap-mips-report.json"),
                                 session_class=GraphicsSession, boundary_name="explosionFlushBlasts",
                                 compare_a0=False, oracle_class=ObjectsOracle)
    rom, manifest = rom_path.read_bytes(), json.loads(manifest_path.read_text())
    with elf_path.open("rb") as file:
        table = ELFFile(file).get_section_by_name(".symtab")
        symbols = {name: unique_symbol(table, name)["st_value"] for name in
                   ("__osDisableInt", "__osRestoreInt", "TrapDanglingJump", "osCreateMesgQueue", "modLoadModel")}
    lib = load_native_library(library_path)
    cases = []
    for tv in (1, 0, 2):
        session = GraphicsSession(lib, rom, manifest, tv, dirty_heap=True)
        try:
            session.bootstrap()
            check_objects(session)
            initial = session.native.snapshot()
            oracle = InitOracle(initial, rom, manifest, symbols)
            oracle.command_queue = session.symbols["gPIMesgQueue"]
            index = session.count
            stack = 0x80780000 - index * 0x2000
            registers = [0] * 32
            registers[4], registers[5] = 35, 0
            registers[29], registers[31] = sx32(stack - 0x10), sx32(RETURN)
            expected_regs = oracle.call(symbols["modLoadModel"], registers, budget=8000000)
            require(oracle.boundary is None, "Asset reference unexpectedly reached a lazy-load trap")
            before = len([e for e in session.native.events() if e[0] == 1])
            actual_regs = session.run_function("modLoadModel", 35, 0)
            compared = (0, 2, *range(16, 24), 28, 29, 30, 31)
            require(all(expected_regs[i] == actual_regs[i] for i in compared), "Asset return/callee-saved GPRs differ")
            transfers = [[e[1], e[2], e[3]] for e in session.native.events() if e[0] == 1][before:]
            require(transfers == oracle.transfers, "Asset ROM transfer trace differs")
            # This extra diagnostic worker did not exist in the bootstrap.
            # Exclude only its kernel slot/stack, the host slot, and DMA queue
            # wait-list links. All actual game allocations/assets stay compared.
            regions = [(HOST, 0x100), (SLOTS + index * 0x200, 0x200),
                       (stack - 0x2000, 0x2040), (session.symbols["gDmaMesgQueue"], 8)]
            expected, actual = masked(oracle.memory(), regions), masked(session.native.snapshot(), regions)
            if expected != actual:
                offset = next(i for i, (a, b) in enumerate(zip(expected, actual)) if a != b)
                raise AssertionError(f"Asset RAM differs at 0x{offset:08X}: {expected[offset:offset+16].hex()} != {actual[offset:offset+16].hex()}")
            details = check_graphics_assets(session, actual_regs[2] & 0xFFFFFFFF)
            cases.append({"name": f"original_asset_load_tv_{tv}", "status": "passed",
                          "compared_gprs": list(compared), "rom_transfers": len(transfers),
                          "initial_ram_sha256": hashlib.sha256(initial).hexdigest(),
                          "compared_ram_sha256": hashlib.sha256(actual).hexdigest(),
                          "masked_regions": [[f"0x{address:08X}", size] for address, size in regions],
                          "assets": details})
        finally:
            joined = session.close()
        require(joined == 4, "Reference diagnostic leaked a worker")
        cases[-1]["threads_joined"] = joined
        print(f"PASS original MIPS model/texture load TV {tv}", flush=True)
    report = {"status": "passed", "bootstrap_cases": len(bootstrap["cases"]), "cases": cases,
              "limits": ["Bootstrap proved independently from original RAM; asset comparison starts from a native pre-load snapshot after that validated bootstrap",
                         "Original MIPS CPU routines with declared ROM/PI/cache/interrupt contracts; no renderer or RSP execution",
                         "One selected model and its texture; caller-saved scratch GPRs and FPR/FCSR are not compared",
                         "Four diagnostic kernel/stack/wait-list regions excluded from asset RAM comparison; game assets and allocations included"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    run(args.elf, args.rom, args.manifest, args.library, args.report)
