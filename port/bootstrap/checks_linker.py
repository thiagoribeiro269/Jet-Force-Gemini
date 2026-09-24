#!/usr/bin/env python3
"""Native checks for the watched game linker and live relocated JALs."""
import argparse
import ctypes
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from checks_bootstrap import BootstrapSession, configure_bootstrap
from checks_init import require, load_native_library


ROOT = Path(__file__).resolve().parents[2]


def run(library_path, rom_path, manifest_path, report_path=None):
    lib = load_native_library(library_path)
    configure_bootstrap(lib)
    lib.jfg_poc_error_target.restype = lib.jfg_poc_error_site.restype = ctypes.c_uint32
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    require(manifest["bootstrap_profile"]["live_relocated_calls"], "Live JAL profile is required")
    session = BootstrapSession(lib, rom, manifest, dirty_heap=True)
    cases = []
    try:
        table_global = session.symbols["overlayTable"]
        count_global = session.symbols["overlayCount"]
        watch = lib.jfg_poc_watch_game_linker
        memory = session.native.memory
        require((session.word(table_global), session.word(count_global)) == (0, 0),
                "Fresh game linker globals are not zero")
        session.call("mainGetCurrentLevel")
        cases.append("zero_globals_pending")

        for table, count in ((0x80001000, 0), (0, 1)):
            session.set(table_global, table)
            session.set(count_global, count)
            require(watch(memory, len(memory), table_global, count_global) == -1,
                    f"Partial linker globals were accepted: {(table, count)}")
        session.set(table_global, 0)
        session.set(count_global, 0)
        # Failed watch requests must preserve the original pending watch.
        session.call("mainGetCurrentLevel")
        cases.append("partial_globals_rejected")

        for table_address, count_address in ((0x7FFFFFFC, count_global),
                                              (table_global, 0x80800000),
                                              (table_global + 1, count_global),
                                              (table_global, table_global)):
            require(watch(memory, len(memory), table_address, count_address) == -1,
                    f"Invalid linker global addresses were accepted: {(table_address, count_address)}")
        cases.append("invalid_global_addresses_rejected")

        for table, count in ((0x7FFFFFFC, 158), (0x807FFFF0, 158), (0x80001000, 159)):
            session.set(table_global, table)
            session.set(count_global, count)
            require(watch(memory, len(memory), table_global, count_global) == -1,
                    f"Invalid linker table was accepted: {(table, count)}")
        session.set(table_global, 0)
        session.set(count_global, 0)
        require(watch(memory, len(memory), table_global, count_global) == 0,
                "Pending watch was not restored after malformed table")
        cases.append("invalid_table_values_rejected")

        boundary = session.bootstrap()
        table, count = session.word(table_global), session.word(count_global)
        require(table != 0 and count == 158, "Original linker did not install a valid table")
        session.call("mainGetCurrentLevel")
        cases.append("valid_bootstrap_table")

        site = int(boundary["overlay36"], 16) + 0x14
        audio_target = int(boundary["overlay25"], 16) + 0x308
        rom_reads = sum(event[0] == 1 for event in session.native.events())
        session.call("mainInitRlo", expected_status=-3)
        require((lib.jfg_poc_error_target(), lib.jfg_poc_error_site()) == (audio_target, site),
                "Already patched JAL did not reach the audio boundary")
        require(sum(event[0] == 1 for event in session.native.events()) == rom_reads,
                "Calling the loaded overlay unexpectedly read the ROM")
        cases.append("patched_jal_reaches_audio_without_rom_read")

        original = session.word(site)
        require(original >> 26 == 3, "Expected runLink-patched JAL in mainInitRlo")
        session.set(site, 0)
        try:
            session.call("mainInitRlo", expected_status=-3)
            require((lib.jfg_poc_error_target(), lib.jfg_poc_error_site()) == (0, site),
                    "Corrupt guest JAL did not report its exact callsite")
        finally:
            session.set(site, original)
        cases.append("corrupt_live_jal_reports_callsite")

        session.set(table_global, 0)
        session.set(count_global, 0)
        session.call("mainGetCurrentLevel", expected_status=-7)
        mode_address = session.symbols["mainGameMode"]
        previous_mode = session.word(mode_address)
        next_mode = previous_mode ^ 0x5A5A5A5A
        session.call("mainSetMode", next_mode, expected_status=-7)
        require(session.word(mode_address) == previous_mode,
                "Invalid linker globals allowed mainSetMode to mutate guest RAM")
        cases.append("cleared_initialized_globals_fail_closed")

        lib.jfg_poc_unbind_game_linker()
        session.call("mainGetCurrentLevel")
        cases.append("unbind_restores_dispatch")
    finally:
        lib.jfg_poc_unbind_game_linker()
        joined = session.close()

    report = {"status": "passed", "cases": cases, "threads_joined": joined,
              "boundary": boundary, "overlay_count": count}
    if report_path is not None:
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    for case in cases:
        print(f"PASS {case}", flush=True)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, default=ROOT / "build/port-bootstrap/native/libjfg_poc.so")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/port-bootstrap/proof/manifest.json")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    run(args.library, args.rom, args.manifest, args.report)
