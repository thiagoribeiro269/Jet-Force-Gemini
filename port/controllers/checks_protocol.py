#!/usr/bin/env python3
"""Validate controller absence, output preservation and deferred SI completion."""
import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from checks_controllers import ControllerSession
from checks_manager import require, require_success, load_native_library

QUEUE, BUFFER, PATTERN, STATUS, OUTPUT = 0x807B0000, 0x807B1000, 0x807B4000, 0x807B4020, 0x807B2000


def run(library_path, rom_path, manifest_path, report_path):
    lib = load_native_library(library_path)
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    s = ControllerSession(lib, rom, manifest)
    cases = []
    try:
        s.queue(QUEUE, BUFFER, 1)
        queue_bytes = s.read(QUEUE, 24)
        for pattern, status in ((QUEUE, STATUS), (PATTERN, QUEUE)):
            s.call("osContInit", QUEUE, pattern, status, expected_status=-15)
            require(s.read(QUEUE, 24) == queue_bytes and s.controllers()["initialized"] == 0,
                    "Discovery output corrupted its own message queue")
        cases.append("outputs_cannot_overlap_queue_metadata")
        s.write(PATTERN, b"\xA5")
        initial = bytes(range(0x20, 0x30))
        s.write(STATUS, initial)
        require(s.call("osContInit", QUEUE, PATTERN, STATUS)[2] == 0, "Controller initialization failed")
        expected = bytearray(initial)
        for i in range(4): expected[i * 4 + 3] = 8
        require(s.read(PATTERN, 1) == b"\0" and s.read(STATUS, 16) == expected, "Absent discovery overwrote type/accessory data")
        cases.append("absent_status_preserves_other_fields")
        s.write(PATTERN, b"\xA5"); s.write(STATUS, initial)
        require(s.call("osContInit", QUEUE, PATTERN, STATUS)[2] == 0, "Repeated initialization failed")
        require(s.read(PATTERN, 1) == b"\xA5" and s.read(STATUS, 16) == initial, "Repeated initialization modified outputs")
        cases.append("repeated_init_does_not_touch_outputs")

        require(s.call("osContStartReadData", QUEUE)[2] == 0, "Read did not start")
        s.call("osContStartReadData", QUEUE, expected_status=-15)
        require(s.controllers()["pending"] == 1 and s.controllers()["reads_started"] == 1,
                "Overlapping read replaced pending work")
        cases.append("overlapping_read_rejected")
        require(lib.jfg_controllers_pump(s.native.memory) == -3 and s.controllers()["pending"] == 1,
                "Missing SI route incorrectly completed the read")
        cases.append("missing_route_preserves_pending_read")
        s.call("osSetEventMesg", 5, QUEUE, 0x12345678)
        require_success(lib.jfg_controllers_pump(s.native.memory), "Deliver SI")
        require(s.call("osRecvMesg", QUEUE, OUTPUT, 0)[2] == 0 and s.word(OUTPUT) == 0x12345678, "SI completion differs")
        require_success(lib.jfg_controllers_pump(s.native.memory), "Pump idle controller")
        require(s.word(QUEUE + 8) == 0 and s.controllers()["si_delivered"] == 1, "Duplicate SI event")
        cases.append("completion_delivered_once")
        require(s.call("osContStartReadData", QUEUE)[2] == 0, "Second read did not start")
        s.call("osSendMesg", QUEUE, 0xDEADBEEF, 0)
        require_success(lib.jfg_controllers_pump(s.native.memory), "Drop full-queue SI notification")
        require(s.controllers()["si_dropped"] == 1 and s.controllers()["pending"] == 0, "Full queue handling differs")
        require(s.call("osRecvMesg", QUEUE, OUTPUT, 0)[2] == 0 and s.word(OUTPUT) == 0xDEADBEEF, "Existing message was overwritten")
        cases.append("full_queue_keeps_existing_message")
        s.call("osContStartReadData", QUEUE)
    finally:
        joined = s.close()
    require(joined == 0, "Protocol fixture unexpectedly created game workers")
    cases.append("pending_read_cancelled_on_session_end")
    report = {"status": "passed", "cases": cases,
              "limits": ["Deliberately absent-port backend; no physical controller, buttons, PIF DMA or boot timer modeled"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS {len(cases)} controller protocol cases", flush=True)
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report)
