#!/usr/bin/env python3
"""Native-only heap/linker startup probe. Uses Python stdlib, not an emulator."""
from __future__ import annotations

import argparse
import array
import ctypes
import hashlib
import json
from pathlib import Path
import platform
import struct
import sys

RAM_SIZE = 0x800000
RETURN = 0x807FF000
ROM_SHA1 = "493ced9008dbe932d6e91179b68e8630cf23a023"


def sx32(value):
    value &= 0xFFFFFFFF
    return value | 0xFFFFFFFF00000000 if value & 0x80000000 else value


def physical(address):
    return address & 0x1FFFFFFF


def require_success(status, operation):
    if status != 0:
        raise RuntimeError(f"{operation} failed with status {status}")


def load_native_library(path):
    library = ctypes.CDLL(str(path.resolve()))
    library.jfg_poc_function_count.restype = ctypes.c_uint32
    library.jfg_poc_address.argtypes = (ctypes.c_char_p,)
    library.jfg_poc_address.restype = ctypes.c_uint32
    library.jfg_poc_unload_section.argtypes = (ctypes.c_uint32,)
    library.jfg_poc_unload_section.restype = ctypes.c_int
    library.jfg_poc_load_section.argtypes = (ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8),
                                           ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t)
    library.jfg_poc_load_section.restype = ctypes.c_int
    library.jfg_poc_run.argtypes = (ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_uint64))
    library.jfg_poc_run.restype = ctypes.c_int
    library.jfg_boot_bind_rom.argtypes = (ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t)
    library.jfg_boot_bind_rom.restype = ctypes.c_int
    library.jfg_boot_event_count.restype = ctypes.c_uint32
    library.jfg_boot_event.argtypes = (ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32))
    library.jfg_boot_event.restype = ctypes.c_int
    library.jfg_poc_bind_game_linker.argtypes = (ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32)
    library.jfg_poc_bind_game_linker.restype = ctypes.c_int
    library.jfg_poc_unbind_game_linker.restype = None
    library.jfg_boot_clear_events.restype = None
    return library


class NativeBoot:
    def __init__(self, library, state, manifest, extended=False):
        if sys.byteorder != "little" or platform.machine().lower() not in ("x86_64", "amd64") or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError("Use an x86-64 Python and library")
        if hashlib.sha1(state["rom"]).hexdigest() != ROM_SHA1:
            raise ValueError("The original verified US ROM is required")
        if library.jfg_poc_function_count() != len(manifest["functions"]):
            raise ValueError("Library and manifest function counts differ")
        self.lib, self.state, self.manifest = library, state, manifest
        self.words = array.array("I", [0]) * (RAM_SIZE // 4)
        self.memory = (ctypes.c_uint8 * RAM_SIZE).from_buffer(self.words)
        self.rom = (ctypes.c_uint8 * len(state["rom"])).from_buffer_copy(state["rom"])
        library.jfg_poc_unbind_game_linker()
        for section in manifest["sections"]:
            require_success(library.jfg_poc_unload_section(section["index"]), "Reset section")
        main = next(s for s in manifest["sections"] if not s["overlay"])
        require_success(library.jfg_poc_load_section(main["index"], main["vram"], self.memory,
                                                   RAM_SIZE, self.rom, len(state["rom"])), "Load main image")
        require_success(library.jfg_boot_bind_rom(self.rom, len(state["rom"])), "Bind ROM")
        self.memory[physical(state["symbols"]["mmExtendedRam"]) ^ 3] = bool(extended)

    def snapshot(self):
        words = array.array("I", self.words)
        words.byteswap()
        return words.tobytes()

    def word(self, address):
        return struct.unpack_from("<I", self.memory, physical(address))[0]

    def invoke(self, name, *arguments, expected_status=0):
        address = self.lib.jfg_poc_address(name.encode())
        if not address:
            raise AssertionError(f"Native function is not registered: {name}")
        registers = [0] * 32
        for index, value in enumerate(arguments, 4): registers[index] = sx32(value)
        registers[29], registers[31] = sx32(self.state["stack_top"]), sx32(RETURN)
        source, output = (ctypes.c_uint64 * 32)(*registers), (ctypes.c_uint64 * 32)()
        status = self.lib.jfg_poc_run(address, self.memory, RAM_SIZE, source, output)
        if status != expected_status:
            raise AssertionError(f"Native {name} returned status {status}, expected {expected_status}")
        return list(output)

    def bind(self):
        symbols = self.state["symbols"]
        require_success(self.lib.jfg_poc_bind_game_linker(self.memory, RAM_SIZE,
                                                        symbols["overlayTable"], symbols["overlayCount"]),
                        "Bind original linker tables")

    def events(self):
        result = []
        for index in range(self.lib.jfg_boot_event_count()):
            item = (ctypes.c_uint32 * 4)()
            require_success(self.lib.jfg_boot_event(index, item), "Read platform event")
            result.append(tuple(item))
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--extended", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    if "library_sha256" in manifest and hashlib.sha256(args.library.read_bytes()).hexdigest() != manifest["library_sha256"]:
        raise ValueError("Library differs from the packaged diagnostic")
    state = {"rom":args.rom.read_bytes(), "stack_top":manifest["boot_profile"]["stack_top"],
             "symbols":manifest["reference_symbols"]}
    native = NativeBoot(load_native_library(args.library), state, manifest, args.extended)
    for name in ("RevealReturnAddresses", "mmInit", "runlinkInitialise"):
        native.invoke(name)
    native.bind()
    if native.word(state["symbols"]["overlayCount"]) != 158:
        raise AssertionError("Original linker tables were not initialized")
    heap_end = native.word(state["symbols"]["mmEndRam"])
    if heap_end != (0x80600000 if args.extended else 0x80400000):
        raise AssertionError("Original heap boundary differs")
    modules = {}
    for number in manifest["boot_profile"]["loaded_modules"]:
        if native.invoke("runlinkDownloadCode", number)[2] != 1:
            raise AssertionError(f"Original module load failed: {number}")
        base = native.invoke("runlinkIsModuleLoaded", number)[2] & 0xFFFFFFFF
        if not 0x80000000 < base < heap_end:
            raise AssertionError(f"Invalid loaded module address: {number}")
        modules[str(number)] = f"0x{base:08X}"
    section44 = next(s for s in manifest["sections"] if s["overlay"] == 44)
    callback_flag = int(modules["44"], 16) + section44["load_size"] + 0x178
    if native.memory[physical(callback_flag) ^ 3] != 0xFF:
        raise AssertionError("Real module initialization callback did not complete")
    report = {"status":"passed", "scope":"native_heap_linker_startup_only", "host_os":platform.system(),
              "host_arch":platform.machine(), "rom_sha1":ROM_SHA1, "overlay_table_slots":158,
              "extended_ram_flag":args.extended, "heap_end":f"0x{heap_end:08X}",
              "modules":modules, "real_init_callback":"_AutoInit00044 -> amAudioLinesReset",
              "rom_reads":sum(e[0]==1 for e in native.events()), "gpu_initialized":False,
              "emulator_used":False, "gameplay_available":False}
    if args.report: args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__': main()
