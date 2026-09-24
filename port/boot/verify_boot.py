#!/usr/bin/env python3
"""Compare native original-game heap/linker startup with a MIPS execution."""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import platform
import struct
import sys

from reference import ROOT, BootOracle, profile, return_address_patches, sx32, physical, RETURN
from native import NativeBoot, load_native_library


def run(manifest_path, library_path, report_path):
    if sys.byteorder != "little" or platform.machine().lower() not in ("x86_64", "amd64"):
        raise RuntimeError("Use an x86-64 host")
    manifest = json.loads(manifest_path.read_text())
    state = profile(Path(manifest["elf_path"]), Path(manifest["rom_path"]))
    library = load_native_library(library_path)
    assert library.jfg_poc_function_count() == len(manifest["functions"])
    variants = []
    for extended in (False, True):
        oracle = BootOracle(state, extended)
        native = NativeBoot(library, state, manifest, extended)
        assert native.snapshot() == oracle.memory()
        before = native.snapshot()
        native.invoke("mmAlloc", 16, 0xFFFFFFFF, expected_status=-9)
        assert native.snapshot() == before
        steps = []

        def checkpoint(name, *arguments):
            oracle.events.clear()
            library.jfg_boot_clear_events()
            if name in state["symbols"]:
                expected = oracle.invoke(name, *arguments)
            else:
                function = next(f for f in manifest["functions"] if f["name"] == name)
                section = manifest["sections"][function["section"]]
                table = oracle.u32(state["symbols"]["overlayTable"])
                base = oracle.u32(table + section["overlay"] * 32)
                registers = [0] * 32
                for index, value in enumerate(arguments, 4): registers[index] = sx32(value)
                registers[29], registers[31] = sx32(state["stack_top"]), sx32(RETURN)
                expected = oracle.call(base + function["offset"], registers, budget=3000000)
            actual = native.invoke(name, *arguments)
            if actual != expected:
                diff = [(i, hex(a), hex(b)) for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
                raise AssertionError(f"{name} register mismatch: {diff}")
            expected_ram, actual_ram = oracle.memory(), native.snapshot()
            if actual_ram != expected_ram:
                address = next(i for i, (a, b) in enumerate(zip(actual_ram, expected_ram)) if a != b)
                raise AssertionError(f"{name} RAM differs at {address:08X}: {actual_ram[address:address+16].hex()} != {expected_ram[address:address+16].hex()}")
            events = native.events()
            if events != oracle.events:
                raise AssertionError(f"{name} platform event trace differs")
            steps.append({"function": name, "arguments": list(arguments), "return_v0": actual[2],
                          "rom_reads": sum(e[0] == 1 for e in events),
                          "rom_bytes": sum(e[3] for e in events if e[0] == 1),
                          "cache_contract_calls": sum(e[0] in (2, 3) for e in events),
                          "ram_sha256": hashlib.sha256(actual_ram).hexdigest()})
            print(f"PASS {'extended' if extended else 'standard'}: {name}{arguments}", flush=True)
            return actual

        checkpoint("RevealReturnAddresses")
        for patch in return_address_patches(state):
            assert oracle.u32(patch["vram"]) == patch["after"]
        expected_code = bytearray(state["image"])
        for patch in return_address_patches(state):
            struct.pack_into(">I", expected_code, physical(patch["vram"]), patch["after"])
        start = physical(state["symbols"]["__CODE_SECTION_START"])
        end = physical(state["symbols"]["__DATA_SECTION_START"])
        assert oracle.memory()[start:end] == expected_code[start:end]
        checkpoint("mmInit")
        heap_end = oracle.u32(state["symbols"]["mmEndRam"])
        assert heap_end == (0x80600000 if extended else 0x80400000)
        checkpoint("runlinkInitialise")
        assert oracle.u32(state["symbols"]["overlayCount"]) == 158
        native.bind()
        loaded = {}
        for number in (19, 6, 32, 44):
            assert checkpoint("runlinkDownloadCode", number)[2] == 1
            base = checkpoint("runlinkIsModuleLoaded", number)[2] & 0xFFFFFFFF
            assert 0x80000000 < base < heap_end
            loaded[number] = base
        section44 = next(s for s in manifest["sections"] if s["overlay"] == 44)
        callback_flag = loaded[44] + section44["load_size"] + 0x178
        assert oracle.cpu.mem_read(physical(callback_flag), 1) == b'\xff'
        checkpoint("pauseGetScreen")
        checkpoint("frontSetInstrumentsHide", 1)
        checkpoint("pauseGetPauseCharacter")
        # Negative fixtures simulate active sound handles. Neither deferred
        # stop path may silently succeed. Restore all RAM before continuing
        # the independent startup/load/unload comparison.
        pristine = bytes(native.memory)
        audio_lines = loaded[44] + section44["load_size"]
        for flag in (0, 1):
            struct.pack_into("<I", native.memory, physical(audio_lines + 0x168), 1)
            native.memory[physical(audio_lines + 0x174) ^ 3] = flag
            native.invoke("amAudioLinesReset", expected_status=-3)
            ctypes.memmove(native.memory, pristine, len(pristine))
        assert native.snapshot() == oracle.memory()
        checkpoint("mmSetDelay", 0)
        for number in (44, 32, 6, 19):
            checkpoint("runlinkUnloadOverlay", number)
            assert checkpoint("runlinkIsModuleLoaded", number)[2] == 0
        assert library.jfg_poc_address(b"pauseGetScreen") == 0
        assert checkpoint("runlinkDownloadCode", 19)[2] == 1
        checkpoint("runlinkIsModuleLoaded", 19)
        assert checkpoint("runlinkDownloadCode", 19)[2] == 1
        assert steps[-1]["rom_reads"] == 0
        checkpoint("runlinkUnloadOverlay", 19)
        checkpoint("mmSetDelay", 2)
        variants.append({"extended_ram_flag": extended, "heap_end": f"0x{heap_end:08X}",
                         "overlay_table_slots": 158, "module_bases": {str(k):f"0x{v:08X}" for k,v in loaded.items()},
                         "real_init_callback": "_AutoInit00044 -> amAudioLinesReset",
                         "premature_allocator_rejections": 1, "deferred_audio_rejections": 2, "steps": steps})
    report = {"status":"passed", "host_os":platform.system(), "host_arch":platform.machine(),
              "checkpoints":sum(len(v["steps"]) for v in variants), "variants":variants,
              "comparison":"All 32 GPRs, all 8 MiB RAM and ordered ROM/cache platform events at each checkpoint",
              "platform_contracts":list(manifest["boot_profile"]["platform_imports"]),
              "deferred_audio_paths":list(manifest["boot_profile"]["deferred_audio_branches"]),
              "runtime_instruction_patches":len(manifest["runtime_patches"]),
              "limits":manifest["limits"]}
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print(f"PASS: {report['checkpoints']} native startup/loader checkpoints",flush=True)
    return report


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,default=ROOT/'build/port-boot/proof/manifest.json')
    p.add_argument('--library',type=Path,required=True)
    p.add_argument('--report',type=Path,default=ROOT/'build/port-boot/report.json')
    a=p.parse_args()
    run(a.manifest,a.library,a.report)
