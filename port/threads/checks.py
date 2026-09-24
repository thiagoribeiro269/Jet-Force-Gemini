#!/usr/bin/env python3
"""Exercise real recompiled game waits and native cooperative thread lifecycles."""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import platform
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE if (HERE / "native.py").exists() else HERE.parent / "boot"))
from native import NativeBoot, load_native_library, require_success, sx32, physical, RAM_SIZE, RETURN

HOST = 0x807A0000
ROOT_STACK = 0x80790000
SLOTS = 0x807A1000
QUEUE, BUFFER, OUTPUT, PAYLOAD = 0x807B0000, 0x807B1000, 0x807B2000, 0x807B3000


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def configure(library):
    byteptr, wordptr, regptr = ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_uint64)
    library.jfg_threads_begin.argtypes = (byteptr, ctypes.c_size_t, ctypes.c_uint32)
    library.jfg_threads_begin.restype = ctypes.c_int
    library.jfg_threads_create.argtypes = (byteptr, ctypes.c_uint32, ctypes.c_int32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int32, regptr)
    library.jfg_threads_create.restype = ctypes.c_int
    library.jfg_threads_result.argtypes = (ctypes.c_uint32, wordptr, wordptr, regptr)
    library.jfg_threads_result.restype = ctypes.c_int
    library.jfg_threads_end.argtypes = (byteptr,)
    library.jfg_threads_end.restype = ctypes.c_int
    library.jfg_threads_joined.restype = ctypes.c_uint32


class Session:
    def __init__(self, library, rom, manifest, initialize_game=True):
        self.lib = library
        self.manifest = manifest
        self.symbols = manifest["reference_symbols"]
        state = {"rom": rom, "symbols": self.symbols, "stack_top": manifest["boot_profile"]["stack_top"]}
        self.native = NativeBoot(library, state, manifest)
        if initialize_game:
            self.initialize_game()
        self.before_host = bytes(self.native.memory[physical(HOST):physical(HOST) + 0x100])
        self.count = 0
        self.closed = False
        require_success(library.jfg_threads_begin(self.native.memory, RAM_SIZE, HOST), "Attach host scheduler")

    def initialize_game(self):
        for name in ("RevealReturnAddresses", "mmInit", "runlinkInitialise"):
            self.native.invoke(name)
        self.native.bind()
        for number in self.manifest["boot_profile"]["loaded_modules"]:
            require(self.native.invoke("runlinkDownloadCode", number)[2] == 1, "Original module load failed")

    def set(self, address, value):
        struct.pack_into("<I", self.native.memory, physical(address), value & 0xFFFFFFFF)

    def word(self, address):
        return self.native.word(address)

    def call(self, name, *arguments, expected_status=0):
        registers = [0] * 32
        for index, value in enumerate(arguments, 4):
            registers[index] = sx32(value)
        registers[29], registers[31] = sx32(ROOT_STACK), sx32(RETURN)
        source, output = (ctypes.c_uint64 * 32)(*registers), (ctypes.c_uint64 * 32)()
        status = self.lib.jfg_poc_run(self.lib.jfg_poc_address(name.encode()), self.native.memory,
                                      RAM_SIZE, source, output)
        require(status == expected_status, f"{name} status {status}, expected {expected_status}")
        return list(output)

    def queue(self, address=QUEUE, buffer=BUFFER, capacity=1):
        self.call("osCreateMesgQueue", address, buffer, capacity)

    def create(self, name, arguments=(), priority=10, guest_create=False):
        slot, stack = SLOTS + self.count * 0x200, 0x80780000 - self.count * 0x2000
        entry = self.lib.jfg_poc_address(name.encode())
        if guest_create:
            self.set(ROOT_STACK + 0x10, stack)
            self.set(ROOT_STACK + 0x14, priority)
            self.call("osCreateThread", slot, self.count + 1, entry, arguments[0] if arguments else 0)
        else:
            args = (ctypes.c_uint64 * 4)(*[sx32(v) for v in (*arguments, *([0] * (4 - len(arguments))))])
            require_success(self.lib.jfg_threads_create(self.native.memory, slot, self.count + 1,
                                                        entry, stack, priority, args), "Create thread")
        self.count += 1
        return slot

    def start(self, slot):
        # The parent dispatcher remains active while the child runs. This
        # catches a shared/global dispatch guard across suspended threads.
        self.call("osStartThread", slot)

    def result(self, slot, expected_state=2, expected_status=0):
        state, status, registers = ctypes.c_int32(), ctypes.c_int32(), (ctypes.c_uint64 * 32)()
        require_success(self.lib.jfg_threads_result(slot, ctypes.byref(state), ctypes.byref(status), registers), "Read thread state")
        require((state.value, status.value) == (expected_state, expected_status),
                f"Unexpected thread state {(state.value, status.value)}, expected {(expected_state, expected_status)}")
        return list(registers)

    def close(self):
        require_success(self.lib.jfg_threads_end(self.native.memory), "Join and detach scheduler")
        joined = self.lib.jfg_threads_joined()
        require(joined == self.count, "Not every created host thread was joined")
        require(bytes(self.native.memory[physical(HOST):physical(HOST) + 0x100]) == self.before_host,
                "Host scheduler slot or its canary was not restored")
        self.closed = True
        return joined


def run(library_path, rom_path, manifest_path, report_path):
    library = load_native_library(library_path)
    configure(library)
    manifest = json.loads(manifest_path.read_text())
    rom = rom_path.read_bytes()
    cases = []

    def finish(session, name, **details):
        joined = session.close()
        cases.append({"name": name, "status": "passed", "threads_joined": joined, **details})
        print(f"PASS {name}: {joined} threads joined", flush=True)

    session = Session(library, rom, manifest)
    session.set(session.symbols["D_800A4034"], 0)
    worker = session.create("rcpWaitDP", guest_create=True)
    session.start(worker)
    registers = session.result(worker)
    require(registers[2] == 0 and registers[29] == sx32(0x80780000 - 0x10), "Immediate game wait return/stack differs")
    finish(session, "game_wait_without_active_dp", return_v0=registers[2])

    for order in ((0, 2, 1), (2, 1, 0)):
        session = Session(library, rom, manifest)
        queue_names = ("D_800FF1C8", "D_800FF628", "refractDoneMsgQueue")
        queues = [session.symbols[name] for name in queue_names]
        for index, queue in enumerate(queues):
            session.queue(queue, BUFFER + index * 16)
        for name in ("D_800A4034", "blurTaskActive", "refractTaskActive"):
            session.set(session.symbols[name], 1)
        session.set(session.symbols["cloneTaskActive"], 0)
        session.set(PAYLOAD + 4, 0x12345678)
        worker = session.create("rcpWaitDP", guest_create=True)
        session.start(worker)
        session.result(worker, 1)
        require(session.word(queues[0]) == worker, "Game routine did not suspend on the DP queue")
        setter = session.create("mainSetMode", (44,), priority=5, guest_create=True)
        session.start(setter)
        session.result(setter)
        require(session.word(session.symbols["mainGameMode"]) == 44, "Second game thread did not run")
        for index, event in enumerate(order):
            require(session.call("osSendMesg", queues[event], PAYLOAD, 0)[2] == 0, "Completion send failed")
            if index != len(order) - 1:
                session.result(worker, 1)
        registers = session.result(worker)
        require(registers[2] == 0x12345678 and registers[29] == sx32(0x80780000 - 0x10), "Resumed game result/stack differs")
        flags = {name: session.word(session.symbols[name]) for name in
                 ("D_800A4034", "blurTaskActive", "refractTaskActive", "cloneTaskActive")}
        require(all(v == 0 for v in flags.values()), "Original game did not clear completion flags")
        require(all(session.word(q + 8) == 0 for q in queues), "Completion messages were not consumed")
        finish(session, "game_wait_event_order_" + "".join(map(str, order)), return_v0=registers[2], flags=flags,
               synthetic_completion_events=list(order))

    for operation, capacity in (("osSendMesg", 1), ("osJamMesg", 2)):
        session = Session(library, rom, manifest)
        session.queue(capacity=capacity)
        for value in range(1, capacity + 1):
            session.call("osSendMesg", QUEUE, value, 0)
        sender = session.create(operation, (QUEUE, 99, 1), priority=10)
        session.start(sender)
        session.result(sender, 1)
        require(session.word(QUEUE + 4) == sender, "Full-queue producer did not suspend")
        receiver = session.create("osRecvMesg", (QUEUE, OUTPUT, 1), priority=5)
        session.start(receiver)
        require(session.result(sender)[2] == 0 and session.result(receiver)[2] == 0, "Producer/consumer did not finish")
        require(session.word(OUTPUT) == 1, "First FIFO message differs")
        remaining = []
        for _ in range(capacity):
            session.call("osRecvMesg", QUEUE, OUTPUT, 0)
            remaining.append(session.word(OUTPUT))
        require(remaining == ([99] if capacity == 1 else [99, 2]), "Resumed send/jam order differs")
        finish(session, "blocking_" + operation, remaining_messages=remaining)

    session = Session(library, rom, manifest)
    session.queue()
    workers = []
    for index, priority in enumerate((5, 10, 7)):
        slot = session.create("osRecvMesg", (QUEUE, OUTPUT + index * 4, 1), priority)
        session.start(slot)
        session.result(slot, 1)
        workers.append(slot)
    for value, index in enumerate((1, 2, 0), 1):
        session.call("osSendMesg", QUEUE, value, 0)
        session.result(workers[index])
        require(session.word(OUTPUT + index * 4) == value, "Message woke the wrong priority")
    finish(session, "priority_order", receive_priority_order=[10, 7, 5])

    # The oldest created worker is the tail of the wait list: cancelling it
    # exercises non-head removal and exception unwinding through generated C.
    session = Session(library, rom, manifest)
    session.queue()
    for index, priority in enumerate((5, 10, 7)):
        slot = session.create("osRecvMesg", (QUEUE, OUTPUT + index * 4, 1), priority)
        session.start(slot)
        session.result(slot, 1)
    finish(session, "cancel_blocked_non_head_threads")
    require(session.word(QUEUE) == 0, "Cancelled receive wait list was not cleared")

    session = Session(library, rom, manifest)
    session.create("mainSetMode", (77,), guest_create=True)
    finish(session, "cancel_created_before_start")

    session = Session(library, rom, manifest)
    session.queue(session.symbols["resetMsgQueue"])
    session.set(session.symbols["resetPressed"], 0)
    require(session.call("mainResetPressed")[2] == 0, "Empty reset queue returned pressed")
    session.call("osSendMesg", session.symbols["resetMsgQueue"], 1, 0)
    require(session.call("mainResetPressed")[2] == 1, "Reset message not consumed by game")
    require(session.call("mainResetPressed")[2] == 1, "Original reset latch did not persist")
    finish(session, "original_reset_queue_consumer")

    session = Session(library, rom, manifest)
    before = session.native.snapshot()
    arguments = (ctypes.c_uint64 * 4)()
    status = library.jfg_threads_create(session.native.memory, SLOTS, 1, 0x80712344, 0x80780000, 10, arguments)
    require(status == -2 and before == session.native.snapshot(), "Unknown entry created a thread or changed RAM")
    for offset in range(0, 24, 4):
        session.set(QUEUE + offset, 0xA5A5A5A5)
    session.queue()
    worker = session.create("osRecvMesg", (QUEUE, OUTPUT, 1))
    session.start(worker)
    session.result(worker, 1)
    before = session.native.snapshot()
    session.call("osCreateMesgQueue", QUEUE, BUFFER, 1, expected_status=-11)
    require(session.native.snapshot() == before, "Recreation discarded a live wait list")
    session.call("osSendMesg", QUEUE, 123, 0)
    session.result(worker)
    require(session.word(OUTPUT) == 123, "Dispatcher did not recover after the controlled error")
    finish(session, "queue_recreation_and_invalid_entry_rejected")

    session = Session(library, rom, manifest)
    dp = session.symbols["D_800FF1C8"]
    session.queue(dp)
    for name, value in (("D_800A4034", 1), ("blurTaskActive", 0), ("refractTaskActive", 0), ("cloneTaskActive", 1)):
        session.set(session.symbols[name], value)
    session.call("osSendMesg", dp, PAYLOAD, 0)
    worker = session.create("rcpWaitDP", guest_create=True)
    session.start(worker)
    session.result(worker, 3, -3)
    finish(session, "unsupported_clone_path_is_controlled_error")

    session = Session(library, rom, manifest)
    child_slot, child_stack = SLOTS + 0x200, 0x8077E000
    entry = library.jfg_poc_address(b"mainSetMode")
    creator = session.create("osCreateThread", (child_slot, 2, entry, 55), priority=20)
    # N64 o32 parameters five and six are in the caller's stack argument area.
    session.set(0x80780000, child_stack)
    session.set(0x80780004, 5)
    session.start(creator)
    session.result(creator)
    session.count += 1  # Created by the running guest import, not by Python.
    session.result(child_slot, 0)
    starter = session.create("osStartThread", (child_slot,), priority=20)
    session.start(starter)
    session.result(starter)
    child = session.result(child_slot)
    require(child[29] == sx32(child_stack - 0x10), "Guest-created child's stack argument differs")
    require(session.word(session.symbols["mainGameMode"]) == 55, "Guest-created child did not execute original game code")
    finish(session, "create_and_start_from_running_guest_thread")

    report = {"status": "passed", "scope": "cooperative_threads_and_real_game_message_consumers",
              "host_os": platform.system(), "host_arch": platform.machine(), "cases": cases,
              "threads_created_and_joined": sum(c["threads_joined"] for c in cases),
              "library_sha256": hashlib.sha256(library_path.read_bytes()).hexdigest(),
              "rom_sha1": hashlib.sha1(rom).hexdigest(), "emulator_used": False,
              "gpu_initialized": False, "full_boot": False,
              "limits": manifest["limits"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS {len(cases)} thread/message scenarios, {report['threads_created_and_joined']} threads joined")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    run(args.library, args.rom, args.manifest, args.report)
