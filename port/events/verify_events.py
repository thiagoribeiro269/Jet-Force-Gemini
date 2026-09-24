#!/usr/bin/env python3
"""Check event tables, timers and idle retraces against original JFG MIPS."""
import argparse
import ctypes
import json
from pathlib import Path
import struct
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE, mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/runtime"))
from verify_queues import QueueOracle, unique_symbol, sx32, RETURN
from checks_events import EventSession, configure_events, require, COUNTER_HZ, TIMER, CLIENT, QUEUE, BUFFER, OUTPUT, ROOT_STACK
from native import load_native_library, physical


class Reference(QueueOracle):
    def __init__(self, image, symbols):
        super().__init__(image, symbols)
        self.ticks = 0
        self.compare = None
        for name in ("osGetCount", "__osSetCompare"):
            address = sx32(symbols[name])
            self.cpu.hook_add(UC_HOOK_CODE, self.hardware, name, begin=address, end=address)

    def hardware(self, cpu, pc, size, name):
        if name == "osGetCount":
            cpu.reg_write(mips_const.UC_MIPS_REG_2, sx32(self.ticks))
        else:
            self.compare = cpu.reg_read(mips_const.UC_MIPS_REG_4) & 0xFFFFFFFF
        cpu.reg_write(mips_const.UC_MIPS_REG_PC, cpu.reg_read(mips_const.UC_MIPS_REG_RA))

    def invoke_name(self, name, *args):
        regs = [0] * 32
        for index, value in enumerate(args, 4): regs[index] = sx32(value)
        regs[29], regs[31] = sx32(ROOT_STACK), sx32(RETURN)
        return self.call(self.symbols[name], regs, budget=3000000)

    def timer(self, address, countdown, interval, queue, message):
        self.write(ROOT_STACK + 0x10, struct.pack(">IIII", interval >> 32, interval & 0xFFFFFFFF, queue, message))
        return self.invoke_name("osSetTimer", address, 0, countdown >> 32, countdown & 0xFFFFFFFF)

    def bytes(self, address, count):
        return bytes(self.cpu.mem_read(physical(address), count))


def run(elf_path, rom_path, manifest_path, library_path, report_path):
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    with elf_path.open("rb") as file:
        symtab = ELFFile(file).get_section_by_name(".symtab")
        symbols = {name: unique_symbol(symtab, name)["st_value"] for name in (
            "__osDisableInt", "__osRestoreInt", "osGetCount", "__osSetCompare", "osSetEventMesg",
            "osSetTime", "osGetTime", "__osTimerServicesInit", "osSetTimer", "__osTimerInterrupt",
            "osCreateMesgQueue", "osSendMesg", "osRecvMesg", "__scHandleRetrace")}
    library = load_native_library(library_path)
    configure_events(library)
    evidence = []

    def record(s, name, checks):
        joined = s.close()
        evidence.append({"name": name, "checks": checks, "status": "passed", "threads_joined": joined})
        print(f"PASS original MIPS {name}: {checks} comparisons", flush=True)

    s = EventSession(library, rom, manifest)
    s.queue()
    oracle = Reference(s.native.snapshot(), symbols)
    table = s.symbols["__osEventStateTab"]
    for event in range(15):
        s.call("osSetEventMesg", event, QUEUE, event + 100)
        oracle.invoke_name("osSetEventMesg", event, QUEUE, event + 100)
        require(s.native.snapshot()[physical(table):physical(table) + 120] == oracle.bytes(table, 120), "Event binding differs from original MIPS")
    s.call("osSetEventMesg", 4, 0, 0)
    oracle.invoke_name("osSetEventMesg", 4, 0, 0)
    require(s.native.snapshot()[physical(table):physical(table) + 120] == oracle.bytes(table, 120), "Disabled binding differs")
    record(s, "event_registration", 16)

    for value in (0, 0x7FFFFFFF80000001, 0xFEDCBA9887654321):
        s = EventSession(library, rom, manifest)
        oracle = Reference(s.native.snapshot(), symbols)
        oracle.invoke_name("__osTimerServicesInit")
        s.call("osSetTime", value >> 32, value & 0xFFFFFFFF)
        oracle.invoke_name("osSetTime", value >> 32, value & 0xFFFFFFFF)
        for ticks in (0, 37):
            s.advance(ticks - oracle.ticks)
            oracle.ticks = ticks
            actual = s.call("osGetTime")
            expected = oracle.invoke_name("osGetTime")
            require(actual[2:4] == expected[2:4], "64-bit time return ABI differs from original")
        record(s, f"time_abi_{value:016X}", 2)

    timer_scenarios = (
        ("one_shot", [(TIMER, 1000, 0, 11)], [999, 1000, 2000], 4),
        ("periodic", [(TIMER, 0, 1000, 22)], [1000, 2000, 11000], 4),
        ("full_queue", [(TIMER, 1000, 0, 33), (TIMER + 32, 2000, 0, 44)], [1000, 2000], 1),
        ("equal_deadlines_forward", [(TIMER, 1000, 0, 1), (TIMER + 32, 1000, 0, 2)], [1000], 4),
        ("equal_deadlines_reverse", [(TIMER + 32, 1000, 0, 2), (TIMER, 1000, 0, 1)], [1000], 4),
    )
    for name, timers, ticks_list, capacity in timer_scenarios:
        s = EventSession(library, rom, manifest)
        s.queue(QUEUE, BUFFER, capacity)
        oracle = Reference(s.native.snapshot(), symbols)
        oracle.invoke_name("__osTimerServicesInit")
        oracle.invoke_name("osCreateMesgQueue", QUEUE, BUFFER, capacity)
        for address, delay, interval, message in timers:
            require(s.timer(address, delay, interval, message=message)[2] == oracle.timer(address, delay, interval, QUEUE, message)[2], "Timer API return differs")
        for ticks in ticks_list:
            s.advance(ticks - oracle.ticks)
            oracle.ticks = ticks
            oracle.invoke_name("__osTimerInterrupt")
            actual = s.native.snapshot()
            for address, size in ((QUEUE + 8, 16), (BUFFER, capacity * 4)):
                require(actual[physical(address):physical(address) + size] == oracle.bytes(address, size),
                        f"Timer public queue state differs from original: {name} at {ticks}")
        record(s, "timer_" + name, len(timers) + len(ticks_list))

    s = EventSession(library, rom, manifest, scheduler=True)
    sc = s.symbols["sc"]
    s.queue(QUEUE, BUFFER, 4)
    s.call("osScAddClient", sc, CLIENT, QUEUE, 2)
    oracle = Reference(s.native.snapshot(), symbols)
    oracle.invoke_name("osCreateMesgQueue", sc + 0x78, sc + 0x90, 8)
    oracle.invoke_name("osCreateMesgQueue", QUEUE, BUFFER, 4)
    for _ in range(3):
        s.advance(COUNTER_HZ // 60)
        oracle.invoke_name("__scHandleRetrace", sc)
        actual = s.native.snapshot()
        for address, size in ((sc + 0x300, 4), (s.symbols["gRetraceCounter64"], 8), (QUEUE + 8, 16), (BUFFER, 16)):
            require(actual[physical(address):physical(address) + size] == oracle.bytes(address, size), "Idle scheduler retrace differs from original MIPS")
    record(s, "original_idle_retrace_body", 3)
    report = {"status": "passed", "cases": evidence, "comparisons": sum(c["checks"] for c in evidence),
              "reference": "Original JFG US MIPS/libultra under controlled count, compare-register and serialized interrupt hooks",
              "limits": ["Compare event table, API returns, timer message ordering and selected scheduler observable memory",
                         "Private timer list/deadline representations and physical interrupt timing are not compared",
                         "Time ABI samples start with base counter zero; host SetTime uses the runtime offset contract",
                         "No GPU, rendering or whole-console timing validation"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    p.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    p.add_argument("--manifest", type=Path, default=ROOT / "build/port-events/proof/manifest.json")
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--report", type=Path, default=ROOT / "build/port-events/mips-report.json")
    a = p.parse_args()
    run(a.elf, a.rom, a.manifest, a.library, a.report)
