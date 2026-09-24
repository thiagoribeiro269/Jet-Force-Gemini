#!/usr/bin/env python3
"""Exercise the live audio worker without pretending to process samples."""
import argparse
import ctypes
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from checks_start import StartSession
from checks_manager import require, require_success, load_native_library

MESSAGE = 0x807B3000


def run(library_path, rom_path, manifest_path, report_path):
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    lib = load_native_library(library_path)
    cases = []
    for message_type in (4, 1):
        session = StartSession(lib, rom, manifest)
        try:
            session.bootstrap()
            profile = manifest["start_profile"]
            queue, slot = profile["thread_queue"], profile["thread_address"]
            before = session.ai()
            # __amMain reads the signed halfword message type at offset zero.
            session.set(MESSAGE, message_type << 16)
            require(session.call("osSendMesg", queue, MESSAGE, 0)[2] == 0, "Audio message was not accepted")
            state, status, _ = session.poll(slot)
            require(session.word(queue + 8) == 0, "Audio worker did not consume the message")
            require(session.ai() == before, "A control message changed AI configuration")
            if message_type == 4:
                require((state, status) == (1, 0) and session.word(queue) == slot,
                        "Original type-4 branch did not return to its empty wait")
                require(session.word(profile["thread_stack_top"] - 0x14) == MESSAGE,
                        "Original receiver did not store the message in its stack slot")
                details = {"name": "audio_worker_wakes_and_waits_again", "message_type": 4}
            else:
                target, site = ctypes.c_uint32(), ctypes.c_uint32()
                require_success(lib.jfg_threads_error(slot, ctypes.byref(target), ctypes.byref(site)), "Read frame boundary")
                expected = next(f["vram"] for f in manifest["functions"] if f["name"] == "__amHandleFrameMsg")
                require((state, status, target.value, site.value) == (3, -3, expected, profile["thread_target"] + 0xB8),
                        f"Unimplemented frame did not fail at its boundary: {(state, status, hex(target.value), hex(site.value))}")
                details = {"name": "audio_frame_remains_explicitly_unimplemented", "message_type": 1,
                           "status_code": status, "target": f"0x{target.value:08X}", "call_site": f"0x{site.value:08X}"}
        finally:
            joined = session.close()
        require(joined == 3 and session.ai()["active"] == 0, "Wake test left owned workers or AI state")
        cases.append({"status": "passed", "threads_joined": joined, **details})
        print(f"PASS {details['name']}", flush=True)
    report = {"status": "passed", "cases": cases,
              "limits": ["Controlled native messages only; no physical retrace, sample synthesis or audio device output"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report)
