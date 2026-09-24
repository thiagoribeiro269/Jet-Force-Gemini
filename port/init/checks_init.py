#!/usr/bin/env python3
"""Run original initialization until the explicit bootstrap boundary and check assets."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import platform
import struct
import sys
import zlib

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE if (HERE / "checks_events.py").exists() else HERE.parent / "events"))
from checks_events import Session, configure_events, require, ROOT_STACK, QUEUE, BUFFER, OUTPUT
from native import load_native_library, require_success, physical

IO_FIELDS = ("active", "command_queue", "pending", "submitted", "completed", "bytes", "dropped", "cancelled", "rejected", "reserved")
MESSAGE, DESTINATION = 0x807B6000, 0x80600000


def configure_init(lib):
    configure_events(lib)
    ptr = ctypes.POINTER(ctypes.c_uint8)
    for name, args in {"begin": (ptr,), "end": (ptr,), "pump": (ptr, ctypes.c_uint32),
                       "state": (ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t)}.items():
        func = getattr(lib, "jfg_io_" + name)
        func.argtypes, func.restype = args, ctypes.c_int
    lib.jfg_threads_error.argtypes = (ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32))
    lib.jfg_threads_error.restype = ctypes.c_int
    lib.jfg_events_vi_state.argtypes = (ctypes.POINTER(ctypes.c_uint32), ctypes.c_size_t)
    lib.jfg_events_vi_state.restype = ctypes.c_int


class InitSession(Session):
    def __init__(self, lib, rom, manifest, tv=1, dirty_heap=False):
        super().__init__(lib, rom, manifest, initialize_game=False)
        self.rom = rom
        if dirty_heap:
            # Poison future payload allocations, leaving allocator metadata
            # alone, so untouched zero-filled RAM cannot satisfy the clear test.
            start = physical(self.symbols["gMainMemoryPool"]) + 0x8000
            ctypes.memset(ctypes.addressof(self.native.memory) + start, 0xA5, 0x400000 - start)
        self.set(self.symbols["osTvType"], tv)
        require_success(lib.jfg_events_begin(self.native.memory, self.symbols["__osEventStateTab"], 1), "Start event service")
        require_success(lib.jfg_io_begin(self.native.memory), "Bind PI transport")

    def io(self):
        values = (ctypes.c_uint64 * 10)()
        require_success(self.lib.jfg_io_state(values, 10), "Read PI state")
        return dict(zip(IO_FIELDS, values))

    def poll(self, slot):
        state, status, output = ctypes.c_int32(), ctypes.c_int32(), (ctypes.c_uint64 * 32)()
        require_success(self.lib.jfg_threads_result(slot, ctypes.byref(state), ctypes.byref(status), output), "Read worker")
        return state.value, status.value, list(output)

    def drive(self, slot):
        for _ in range(1024):
            state, status, result = self.poll(slot)
            if state >= 2: return state, status, result
            processed = self.lib.jfg_io_pump(self.native.memory, 64)
            require(processed > 0, f"Worker cannot progress: PI status {processed}, {self.io()}")
        raise AssertionError("PI transfer budget exhausted")

    def run_function(self, name, *args):
        slot = self.create(name, args)
        self.start(slot)
        state, status, result = self.drive(slot)
        require((state, status) == (2, 0), f"{name} failed: {(state, status)}")
        return result

    def bootstrap(self):
        slot = self.create("mainInitGame")
        self.start(slot)
        self.count += 1  # osCreateScheduler starts the original __scMain thread.
        state, status, _ = self.drive(slot)
        target, site = ctypes.c_uint32(), ctypes.c_uint32()
        require_success(self.lib.jfg_threads_error(slot, ctypes.byref(target), ctypes.byref(site)), "Read bootstrap boundary")
        boundary = self.manifest["init_profile"]
        require((state, status, target.value, site.value) ==
                (3, -3, boundary["bootstrap_target"], boundary["bootstrap_call_site"]),
                f"Unexpected initialization stop: {(state, status, hex(target.value), hex(site.value))}")
        self.native.bind()
        return {"function": "mainInitGame", "status": status, "target": f"0x{target.value:08X}", "call_site": f"0x{site.value:08X}"}

    def read(self, address, count):
        return self.native.snapshot()[physical(address):physical(address) + count]

    def dma(self, mb, source, destination, size, queue=QUEUE, priority=0, direction=0, expected_status=0):
        self.set(ROOT_STACK + 0x10, destination)
        self.set(ROOT_STACK + 0x14, size)
        self.set(ROOT_STACK + 0x18, queue)
        return self.call("osPiStartDma", mb, priority, direction, source, expected_status=expected_status)


def run(library_path, rom_path, manifest_path, report_path):
    lib = load_native_library(library_path)
    configure_init(lib)
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    cases = []

    def finish(s, name, **details):
        before = s.io()
        joined = s.close()
        after = s.io()
        require(after["active"] == after["pending"] == 0, "PI work survived shutdown")
        cases.append({"name": name, "status": "passed", "threads_joined": joined, "io": before, **details})
        print(f"PASS {name}: {before['completed']} DMA transfers, {joined} threads joined", flush=True)

    # The US ROM's default resolution table keeps 320x240 for all TV types;
    # PAL changes timing/scaling and the allocation reserve, not this size.
    for tv, height, hz in ((1, 240, 60), (0, 240, 50), (2, 240, 60)):
        s = InitSession(lib, rom, manifest, tv, dirty_heap=True)
        boundary = s.bootstrap()
        syms = s.symbols
        frame_size = 320 * height * 2
        framebuffers = [s.word(syms["framebufferPointers_"] + index * 4) for index in (0, 1)]
        require(framebuffers[0] != framebuffers[1] and all(p % 64 == 0 for p in framebuffers), "Framebuffer layout differs")
        require(s.word(syms["viFramesPerSecond"]) == hz, "Original video rate differs")
        require(s.word(syms["framebufferSize"]) == frame_size + 0x30, "Original Z-buffer allocation size differs")
        require(s.word(syms["otherZbuf"]) % 64 == 0 and s.word(syms["otherZbuf"]) != 0, "Z-buffer alignment differs")
        for pointer in framebuffers:
            require(s.read(pointer, frame_size) == bytes(frame_size), "Original framebuffer clear differs")
        table_start, table_end = syms["__ASSETS_LUT_START"], syms["__ASSETS_LUT_END"]
        require(s.read(s.word(syms["gAssetsLookupTable"]), table_end - table_start) == rom[table_start:table_end], "Asset lookup table was not DMA-loaded")
        require(s.word(syms["overlayCount"]) == 158, "Original runLink was not initialized")
        require(s.word(syms["securitybuffer"]) and s.word(syms["rzip_huft_alloc"]), "Original startup allocations missing")
        vi = (ctypes.c_uint32 * 4)()
        require_success(lib.jfg_events_vi_state(vi, 4), "Read VI control state")
        require(vi[2] == 0x52 and (vi[1] & (8 | 16 | 0x10000 | 0x300)) == (16 | 0x10000),
                "VI special-feature control bits differ")
        finish(s, f"original_main_init_prefix_tv_{tv}", boundary=boundary, dimensions=[320, height],
               framebuffers=[f"0x{x:08X}" for x in framebuffers])

    s = InitSession(lib, rom, manifest)
    s.bootstrap()
    table = struct.unpack(">76I", rom[s.symbols["__ASSETS_LUT_START"]:s.symbols["__ASSETS_LUT_END"]])
    asset = 7
    source = s.symbols["__ASSETS_LUT_END"] + table[asset + 1]
    size = table[asset + 2] - table[asset + 1]
    require(s.call("piRomGetFileSize", asset)[2] == size, "File-size helper differs")
    pointer = s.run_function("piRomLoad", asset)[2] & 0xFFFFFFFF
    require(s.read(pointer, size) == rom[source:source + size], "Original raw asset load differs")
    s.run_function("piRomLoadSection", 19, DESTINATION, 16, 0x5020)
    section_source = s.symbols["__ASSETS_LUT_END"] + table[20] + 16
    require(s.read(DESTINATION, 0x5020) == rom[section_source:section_source + 0x5020], "Section transfer/chunk boundary differs")
    asset = manifest["init_profile"]["compressed_asset_index"]
    start = s.symbols["__ASSETS_LUT_END"] + table[asset + 1]
    end = s.symbols["__ASSETS_LUT_END"] + table[asset + 2]
    expected = zlib.decompress(rom[start + 5:end], -15)
    require(len(expected) == int.from_bytes(rom[start:start + 4], "little"), "Compressed fixture header differs")
    packed_pointer = s.run_function("piRomLoad", asset)[2] & 0xFFFFFFFF
    pointer = s.call("mmAlloc", len(expected), 0x7F7F7FFF)[2] & 0xFFFFFFFF
    require(pointer != 0, "Decompression output allocation failed")
    require(s.run_function("rzipUncompress", packed_pointer, pointer)[2] & 0xFFFFFFFF == pointer,
            "Original decompressor did not return its output buffer")
    require(s.read(pointer, len(expected)) == expected, "Original native decompression differs from zlib")
    require(s.run_function("piRomLoad", table[0] + 1)[2] == 0, "Invalid asset did not return null")
    finish(s, "raw_partial_and_compressed_assets", compressed_asset=asset, decompressed_bytes=len(expected),
           decompressed_sha256=hashlib.sha256(expected).hexdigest())

    s = InitSession(lib, rom, manifest)
    s.queue(QUEUE, BUFFER, 4)
    s.call("osCreatePiManager", 150, QUEUE + 0x40, BUFFER + 0x40, 2)
    s.dma(MESSAGE, 0x1000, DESTINATION, 16)
    s.dma(MESSAGE + 32, 0x1020, DESTINATION + 32, 16, priority=1)
    require(s.dma(MESSAGE + 64, 0x1040, DESTINATION + 64, 16)[2] & 0xFFFFFFFF == 0xFFFFFFFF,
            "Full PI command queue did not return failure")
    s.dma(MESSAGE, 0x1060, DESTINATION, 16, expected_status=-13)
    require(s.io()["pending"] == 2 and s.read(DESTINATION, 16) == bytes(16), "DMA completed before owner service")
    require(lib.jfg_io_pump(s.native.memory, 1) == 1, "Priority DMA did not run")
    s.call("osRecvMesg", QUEUE, OUTPUT, 0)
    require(s.word(OUTPUT) == MESSAGE + 32 and s.read(DESTINATION + 32, 16) == rom[0x1020:0x1030], "High-priority completion differs")
    require(lib.jfg_io_pump(s.native.memory, 1) == 1, "Normal DMA did not run")
    s.call("osRecvMesg", QUEUE, OUTPUT, 0)
    require(s.word(OUTPUT) == MESSAGE and s.read(DESTINATION, 16) == rom[0x1000:0x1010], "Normal DMA completion differs")
    s.dma(MESSAGE, 0x2000000, DESTINATION, 16, expected_status=-13)
    s.dma(MESSAGE, 0x1000, DESTINATION, 16, direction=1, expected_status=-13)
    s.dma(MESSAGE, 0x1000, DESTINATION + 1, 16, expected_status=-13)
    require(s.io()["pending"] == 0, "Rejected DMA left pending work")
    finish(s, "pi_priorities_completion_and_rejections")

    s = InitSession(lib, rom, manifest)
    s.queue()
    s.call("osCreatePiManager", 150, QUEUE + 0x40, BUFFER + 0x40, 2)
    s.call("osSendMesg", QUEUE, 0x1234, 0)
    s.dma(MESSAGE, 0x10001000, DESTINATION, 16)
    require(lib.jfg_io_pump(s.native.memory, 1) == 1, "Cart-address DMA failed")
    require(s.read(DESTINATION, 16) == rom[0x1000:0x1010] and s.io()["dropped"] == 1,
            "Full completion queue did not preserve the actual transfer")
    s.call("osRecvMesg", QUEUE, OUTPUT, 0)
    require(s.word(OUTPUT) == 0x1234, "DMA completion replaced an existing message")
    finish(s, "cart_address_and_full_completion_queue")

    s = InitSession(lib, rom, manifest)
    s.queue()
    s.call("osCreatePiManager", 150, QUEUE + 0x40, BUFFER + 0x40, 2)
    s.dma(MESSAGE, 0x1000, DESTINATION, 16)
    worker = s.create("osRecvMesg", (QUEUE, OUTPUT, 1))
    s.start(worker)
    s.result(worker, 1)
    finish(s, "close_cancels_pending_dma_and_waiter")

    report = {"status": "passed", "scope": "original_init_prefix_video_buffers_pi_dma_and_assets",
              "host_os": platform.system(), "host_arch": platform.machine(), "cases": cases,
              "threads_created_and_joined": sum(c["threads_joined"] for c in cases),
              "library_sha256": hashlib.sha256(library_path.read_bytes()).hexdigest(),
              "rom_sha1": hashlib.sha1(rom).hexdigest(), "emulator_used": False,
              "gpu_initialized": False, "full_main_init": False, "limits": manifest["limits"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS {len(cases)} initialization/DMA scenarios; {report['threads_created_and_joined']} threads joined")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report)
