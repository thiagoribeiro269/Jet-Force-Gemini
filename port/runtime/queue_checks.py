#!/usr/bin/env python3
"""Replay original-MIPS queue expectations using the native runtime and stdlib."""
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
QUEUE = 0x80700000
BUFFER = QUEUE + 0x100
OUTPUT = QUEUE + 0x200
STACK = 0x807F0000
RESULT_SENTINEL = 0x13579BDF
ROM_SHA1 = "493ced9008dbe932d6e91179b68e8630cf23a023"


def physical(address):
    return address & 0x1FFFFFFF


def initial_image(rom, metadata):
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise ValueError("Provide the original verified US ROM")
    image = bytearray(RAM_SIZE)
    size, source, address = metadata["size"], metadata["rom"], physical(metadata["vram"])
    image[address:address + size] = rom[source:source + size]
    image[physical(QUEUE):physical(QUEUE) + 0x1000] = b"\xA5" * 0x1000
    return image


def observable_memory(image):
    # Host/runtime queue internals use null instead of libultra's sentinel
    # thread. Host API calls do not use the guest function's stack frames.
    image = bytearray(image)
    image[physical(QUEUE):physical(QUEUE) + 8] = bytes(8)
    image[physical(STACK) - 0x200:physical(STACK) + 0x40] = bytes(0x240)
    return image


class NativeQueues:
    def __init__(self, library, image):
        if (sys.byteorder != "little" or platform.machine().lower() not in ("amd64", "x86_64")
                or ctypes.sizeof(ctypes.c_void_p) != 8):
            raise RuntimeError("Use x86-64 Python and library")
        self.lib = library
        self.words = array.array("I")
        self.words.frombytes(image)
        self.words.byteswap()
        self.memory = (ctypes.c_uint8 * RAM_SIZE).from_buffer(self.words)
        library.jfg_queue_call.argtypes = (ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
                                          ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int32,
                                          ctypes.POINTER(ctypes.c_int32))
        library.jfg_queue_call.restype = ctypes.c_int

    def snapshot(self):
        words = array.array("I", self.words)
        words.byteswap()
        return words.tobytes()

    def call(self, operation, queue, value, argument):
        output = ctypes.c_int32(RESULT_SENTINEL)
        status = self.lib.jfg_queue_call(operation, self.memory, RAM_SIZE, queue, value, argument,
                                         ctypes.byref(output))
        return status, output.value


def replay(library_path, rom_path, fixtures_path, report_path):
    fixtures_bytes = fixtures_path.read_bytes()
    fixtures = json.loads(fixtures_bytes)
    library = ctypes.CDLL(str(library_path.resolve()))
    image = initial_image(rom_path.read_bytes(), fixtures["main_image"])
    checks, rejected, public_return_checks = 0, 0, 0
    for scenario in fixtures["scenarios"]:
        native = NativeQueues(library, image)
        for step in scenario["steps"]:
            before = native.snapshot() if step["status"] else None
            status, result = native.call(*step["call"])
            if status != step["status"] or result != step["result"]:
                raise AssertionError(f"Queue result differs: {step['call']}, {(status, result)}")
            after = native.snapshot()
            if status:
                if after != before:
                    raise AssertionError("Rejected operation changed RAM")
                rejected += 1
            else:
                public_return_checks += step["call"][0] != 0
                if hashlib.sha256(observable_memory(after)).hexdigest() != step["ram_sha256"]:
                    raise AssertionError(f"Runtime queue RAM differs: {step['call']}")
            checks += 1
    report = {"status": "passed", "scope": "native_runtime_queues_without_thread_suspension",
              "host_os": platform.system(), "host_arch": platform.machine(),
              "checks": checks, "rejections": rejected, "public_return_checks": public_return_checks,
              "capacities": [s["capacity"] for s in fixtures["scenarios"]],
              "comparison": fixtures["comparison"],
              "fixture_sha256": hashlib.sha256(fixtures_bytes).hexdigest(),
              "library_sha256": hashlib.sha256(library_path.read_bytes()).hexdigest(),
              "emulator_used": False, "threads_started": 0, "gpu_initialized": False}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    replay(args.library, args.rom, args.fixtures, args.report)
