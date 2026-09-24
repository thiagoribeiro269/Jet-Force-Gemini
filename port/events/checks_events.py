#!/usr/bin/env python3
"""Exercise original scheduler startup, event routing and native timer wakeups."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import platform
import sys
import time

HERE = Path(__file__).resolve().parent
if (HERE / "thread_checks.py").exists():
    sys.path.insert(0, str(HERE))
    from thread_checks import Session, configure, require, ROOT_STACK, QUEUE, BUFFER, OUTPUT, PAYLOAD
else:
    sys.path.insert(0, str(HERE.parent / "threads"))
    from checks import Session, configure, require, ROOT_STACK, QUEUE, BUFFER, OUTPUT, PAYLOAD
from native import load_native_library, require_success, physical

COUNTER_HZ = 46875000
TIMER = 0x807B5000
CLIENT = 0x807B4000
STATE_FIELDS = ("active", "timers", "timer_firings", "vi_fields", "delivered", "dropped", "posts", "vi_manager",
                "vi_mode", "black", "retrace_divisor", "int_mask", "ticks", "deterministic", "vi_period", "reserved")


def configure_events(lib):
    configure(lib)
    pointer = ctypes.POINTER(ctypes.c_uint8)
    for name, args in {
        "begin": (pointer, ctypes.c_uint32, ctypes.c_int), "advance": (pointer, ctypes.c_uint64),
        "wait": (pointer, ctypes.c_uint32), "post": (pointer, ctypes.c_uint32),
        "cancel_timer": (pointer, ctypes.c_uint32),
        "state": (ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t),
    }.items():
        func = getattr(lib, "jfg_events_" + name)
        func.argtypes, func.restype = args, ctypes.c_int


class EventSession(Session):
    def __init__(self, lib, rom, manifest, *, deterministic=True, scheduler=False, mode=0, retraces=1):
        super().__init__(lib, rom, manifest, initialize_game=False)
        require_success(lib.jfg_events_begin(self.native.memory, self.symbols["__osEventStateTab"], int(deterministic)), "Start events")
        self.scheduler_slot = None
        if scheduler:
            self.set(ROOT_STACK + 0x10, retraces)
            self.call("osCreateScheduler", self.symbols["sc"], self.symbols["Time"], 13, mode)
            self.count += 1
            self.scheduler_slot = self.symbols["sc"] + manifest["event_profile"]["scheduler_layout"]["thread"]
            self.result(self.scheduler_slot, 1)
        # Match the US ordering: the original scheduler starts before heap/runLink.
        self.initialize_game()

    def state(self):
        data = (ctypes.c_uint64 * 16)()
        require_success(self.lib.jfg_events_state(data, 16), "Read event state")
        return dict(zip(STATE_FIELDS, data))

    def advance(self, ticks):
        require_success(self.lib.jfg_events_advance(self.native.memory, ticks), "Advance test clock")

    def post(self, event):
        require_success(self.lib.jfg_events_post(self.native.memory, event), "Post guest event")

    def timer(self, address, countdown, interval=0, queue=QUEUE, message=1, expected_status=0):
        self.set(ROOT_STACK + 0x10, interval >> 32)
        self.set(ROOT_STACK + 0x14, interval)
        self.set(ROOT_STACK + 0x18, queue)
        self.set(ROOT_STACK + 0x1C, message)
        return self.call("osSetTimer", address, 0, countdown >> 32, countdown & 0xFFFFFFFF,
                         expected_status=expected_status)

    def clock(self):
        regs = self.call("osGetTime")
        return ((regs[2] & 0xFFFFFFFF) << 32) | (regs[3] & 0xFFFFFFFF)


def run(library_path, rom_path, manifest_path, report_path):
    lib = load_native_library(library_path)
    configure_events(lib)
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    cases = []

    def new(**kwargs): return EventSession(lib, rom, manifest, **kwargs)

    def finish(session, name, **data):
        before = session.state()
        joined = session.close()
        after = session.state()
        require(after["active"] == after["timers"] == after["vi_manager"] == 0, "Event services survived close")
        cases.append({"name": name, "status": "passed", "threads_joined": joined,
                      "timer_firings": before["timer_firings"], "vi_fields": before["vi_fields"], **data})
        print(f"PASS {name}: {joined} threads joined", flush=True)

    for mode, hz in ((0, 60), (14, 50), (28, 60)):
        s = new(scheduler=True, mode=mode)
        sc = s.symbols["sc"]
        s.queue(QUEUE, BUFFER, 8)
        s.call("osScAddClient", sc, CLIENT, QUEUE, 2)
        period = COUNTER_HZ // hz
        require(s.state()["black"] == 1 and s.state()["vi_period"] == period, "Headless VI mode differs")
        require(s.call("osScGetInterruptQ", sc)[2] & 0xFFFFFFFF == sc + 0x40, "Original interrupt queue offset differs")
        for expected in (1, 2, 3):
            s.advance(period)
            s.result(s.scheduler_slot, 1)
            require(s.word(sc + 0x300) == expected, "Original frame counter did not advance")
            require(s.call("osRecvMesg", QUEUE, OUTPUT, 0)[2] == 0 and s.word(OUTPUT) == sc, "Original video client was not notified")
        s.call("osScRemoveClient", sc, CLIENT)
        s.advance(period)
        require(s.call("osRecvMesg", QUEUE, OUTPUT, 0)[2] & 0xFFFFFFFF == 0xFFFFFFFF, "Removed client still receives retraces")
        finish(s, f"original_scheduler_mode_{mode}", nominal_hz=hz, game_frames=s.word(sc + 0x300))

    s = new(scheduler=True, retraces=2)
    sc = s.symbols["sc"]
    s.advance(COUNTER_HZ // 60)
    require(s.word(sc + 0x300) == 0, "VI divisor fired too soon")
    s.advance(COUNTER_HZ // 60)
    require(s.word(sc + 0x300) == 1, "VI divisor failed")
    finish(s, "vi_retrace_divisor")

    s = new(scheduler=True)
    s.queue(s.symbols["resetMsgQueue"], BUFFER)
    s.call("osScAddClient", s.symbols["sc"], CLIENT, s.symbols["resetMsgQueue"], 3)
    s.set(s.symbols["resetPressed"], 0)
    s.post(14)  # Guest PRENMI only: no host OS shutdown is requested.
    s.result(s.scheduler_slot, 1)
    require(s.call("mainResetPressed")[2] == 1, "Original scheduler did not route PRENMI to game reset")
    finish(s, "guest_prenmi_to_original_reset_consumer")

    s = new()
    s.queue(QUEUE, BUFFER, 16)
    for event in range(15):
        s.call("osSetEventMesg", event, QUEUE, 1000 + event)
        s.post(event)
        require(s.word(s.symbols["__osEventStateTab"] + event * 8) == QUEUE, "Guest event table queue differs")
    for expected in range(1000, 1015):
        s.call("osRecvMesg", QUEUE, OUTPUT, 0)
        require(s.word(OUTPUT) == expected, "Event ordering/message differs")
    s.call("osSetEventMesg", 4, 0, 77)
    s.post(4)
    require(s.word(QUEUE + 8) == 0, "Disabled event was delivered")
    s.call("osSetEventMesg", 15, QUEUE, 1, expected_status=-12)
    finish(s, "all_event_bindings_disable_and_invalid_id")

    s = new()
    s.queue()
    s.timer(TIMER, 1000, message=0x81234567)
    worker = s.create("osRecvMesg", (QUEUE, OUTPUT, 1))
    s.start(worker)
    s.result(worker, 1)
    s.advance(999)
    s.result(worker, 1)
    s.advance(1)
    s.result(worker)
    require(s.word(OUTPUT) == 0x81234567 and s.state()["timers"] == 0, "One-shot timer did not wake the receiver")
    finish(s, "one_shot_timer_resumes_thread")

    s = new()
    s.queue(QUEUE, BUFFER, 8)
    s.timer(TIMER, 0, 1000, message=10)
    s.advance(1000)
    s.advance(1000)
    s.advance(9000)  # Coalesce missed intervals; do not emit an unbounded backlog.
    require(s.word(QUEUE + 8) == 3 and s.state()["timer_firings"] == 3, "Periodic timer semantics differ")
    require(lib.jfg_events_cancel_timer(s.native.memory, TIMER) == 1, "Active timer was not cancelled")
    require(lib.jfg_events_cancel_timer(s.native.memory, TIMER) == 0, "Inactive timer reported cancellation")
    s.advance(1000)
    require(s.word(QUEUE + 8) == 3, "Cancelled timer delivered a message")
    s.timer(TIMER, 10, message=20)
    s.advance(10)
    require(s.word(QUEUE + 8) == 4, "Cancelled timer address could not be reused")
    finish(s, "periodic_timer_cancel_and_reuse")

    s = new()
    s.queue()
    s.call("osSendMesg", QUEUE, 44, 0)
    s.timer(TIMER, 1, message=55)
    s.advance(1)
    require(s.state()["dropped"] == 1 and s.state()["timers"] == 0, "Full queue did not drop expired one-shot")
    s.call("osRecvMesg", QUEUE, OUTPUT, 0)
    require(s.word(OUTPUT) == 44, "Timer overwrote an existing message")
    finish(s, "timer_full_queue_drop")

    s = new()
    s.queue()
    s.timer(TIMER, 0x100000011, 0x200000022, message=66)
    require(s.word(TIMER + 8) == 2 and s.word(TIMER + 12) == 0x22, "64-bit interval stack ABI differs")
    s.advance(0xFFFFFFFF)
    require(s.call("osGetCount")[2] & 0xFFFFFFFF == 0xFFFFFFFF and s.word(QUEUE + 8) == 0, "32-bit count or long timer differs")
    s.advance(0x12)
    require(s.call("osGetCount")[2] == 0x11 and s.word(QUEUE + 8) == 1, "Count wrap/64-bit timer deadline differs")
    s.call("osSetTime", 0xFEDCBA98, 0x87654321)
    require(s.clock() == 0xFEDCBA9887654321, "64-bit time ABI differs")
    s.advance(7)
    require(s.clock() == 0xFEDCBA9887654328, "SetTime did not preserve elapsed ticks")
    require(s.state()["timers"] == 1, "SetTime altered monotonic timer scheduling")
    s.timer(TIMER, 1, expected_status=-12)
    finish(s, "counter_wrap_time_and_64bit_timer_abi")

    s = new()
    s.queue()
    s.timer(TIMER + 32, 1000, message=2)
    s.timer(TIMER, 1000, message=1)
    s.advance(1000)
    # Both deadlines are serviced, but capacity one preserves only the first.
    s.call("osRecvMesg", QUEUE, OUTPUT, 0)
    require(s.word(OUTPUT) == 1 and s.state()["dropped"] == 1, "Equal-deadline insertion ordering differs")
    finish(s, "equal_deadline_ordering")

    s = new()
    s.queue()
    s.timer(TIMER, 1000000)
    worker = s.create("osRecvMesg", (QUEUE, OUTPUT, 1))
    s.start(worker)
    s.result(worker, 1)
    finish(s, "close_cancels_pending_timer_and_waiter")

    s = new(deterministic=False)
    s.queue()
    s.timer(TIMER, COUNTER_HZ // 100, message=88)
    worker = s.create("osRecvMesg", (QUEUE, OUTPUT, 1))
    s.start(worker)
    deadline = time.monotonic() + 2
    while s.state()["timer_firings"] == 0 and time.monotonic() < deadline:
        require_success(lib.jfg_events_wait(s.native.memory, 100), "Wait for monotonic timer")
    s.result(worker)
    require(s.word(OUTPUT) == 88 and s.state()["ticks"] >= COUNTER_HZ // 100, "Real clock timer did not wake the original queue path")
    require(lib.jfg_events_advance(s.native.memory, 1) == -1, "Real clock accepted manual time mutation")
    finish(s, "monotonic_host_clock_timer")

    s = new(scheduler=True)
    s.post(4)
    s.result(s.scheduler_slot, 3, -3)
    finish(s, "unimplemented_rsp_completion_is_explicit_error")

    s = new()
    s.queue()
    s.advance(0xFFFFFFFFFFFFFFFF)
    s.timer(TIMER, 1, expected_status=-12)
    require(s.state()["timers"] == 0, "Overflowing deadline installed a timer")
    require(lib.jfg_events_advance(s.native.memory, 1) == -1, "Manual clock overflow was accepted")
    s.timer(TIMER, 0, message=99)
    s.advance(0)
    require(s.word(QUEUE + 8) == 1 and s.state()["timers"] == 0, "Terminal-tick one-shot did not complete")
    finish(s, "clock_overflow_and_terminal_tick")

    report = {"status": "passed", "scope": "headless_original_scheduler_events_and_timers",
              "host_os": platform.system(), "host_arch": platform.machine(), "cases": cases,
              "threads_created_and_joined": sum(c["threads_joined"] for c in cases),
              "counter_hz": COUNTER_HZ, "clock_modes": ["deterministic_test", "steady_host"],
              "background_timer_threads": 0, "emulator_used": False, "gpu_initialized": False, "full_boot": False,
              "library_sha256": hashlib.sha256(library_path.read_bytes()).hexdigest(), "limits": manifest["limits"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS {len(cases)} event/timer scenarios; {report['threads_created_and_joined']} threads joined")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report)
