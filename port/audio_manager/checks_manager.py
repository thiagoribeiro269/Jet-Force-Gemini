#!/usr/bin/env python3
"""Run original audio manager creation up to the first sequence-player constructor."""
import argparse
import ctypes
import json
from pathlib import Path
import platform
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE if (HERE / "checks_audio.py").exists() else HERE.parent / "audio_init"))
from checks_audio import BootstrapSession, configure_bootstrap, require, require_success, load_native_library, check_audio_assets

AI_FIELDS = ("active", "clock", "requested", "actual", "dac", "bitrate", "control", "writes", "rejected", "write0", "write1", "write2")
CLOCKS = {0: 49656530, 1: 48681812, 2: 48628316}


def configure_manager(lib):
    configure_bootstrap(lib)
    lib.jfg_ai_begin.argtypes = (ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32)
    lib.jfg_ai_begin.restype = ctypes.c_int
    lib.jfg_ai_state.argtypes = (ctypes.POINTER(ctypes.c_uint32), ctypes.c_size_t)
    lib.jfg_ai_state.restype = ctypes.c_int


class ManagerSession(BootstrapSession):
    profile_key = "manager_profile"
    expected_audio_state = 0
    def __init__(self, lib, rom, manifest, tv=1, dirty_heap=False):
        configure_manager(lib)
        super().__init__(lib, rom, manifest, tv, dirty_heap=dirty_heap)
        # __osInitialize runs before mainInitGame on the console. Represent its
        # documented clock selection as initial state, not an AI return stub.
        self.set(self.symbols["osViClock"], CLOCKS[tv])
        require_success(lib.jfg_ai_begin(self.native.memory, self.symbols["osViClock"]), "Bind AI configuration")

    def ai(self):
        fields = (ctypes.c_uint32 * len(AI_FIELDS))()
        require_success(self.lib.jfg_ai_state(fields, len(fields)), "Read AI configuration")
        return dict(zip(AI_FIELDS, fields))

    def boundary_address(self):
        profile = self.manifest[self.profile_key]
        function = next(f for f in self.manifest["functions"] if f["name"] == profile["boundary_function"])
        section = self.manifest["sections"][function["section"]]
        require(profile["boundary_overlay"] == section["overlay"] and
                profile["boundary_target"] == function["vram"], "Boundary metadata differs from the selected function")
        if section["overlay"] == 0:
            return function["vram"]
        base = self.word(self.word(self.symbols["overlayTable"]) + section["overlay"] * 32)
        require(0x80000000 <= base <= 0x80800000 - section["size"], "Boundary overlay was not loaded into guest RAM")
        require(profile["boundary_offset"] == function["offset"], "Boundary offset differs from its overlay")
        return base + function["offset"]

    def bootstrap(self):
        slot = self.create("mainInitGame")
        self.start(slot)
        self.count += 1  # Scheduler thread is started by original startup.
        state, status, _ = self.drive(slot)
        audio_slot = self.symbols["D_800F17C0_B9770"]
        thread_state, thread_status = ctypes.c_int32(), ctypes.c_int32()
        regs = (ctypes.c_uint64 * 32)()
        created = self.lib.jfg_threads_result(audio_slot, ctypes.byref(thread_state), ctypes.byref(thread_status), regs) == 0
        self.count += int(created)
        target, site = ctypes.c_uint32(), ctypes.c_uint32()
        require_success(self.lib.jfg_threads_error(slot, ctypes.byref(target), ctypes.byref(site)), "Read player boundary")
        table = self.word(self.symbols["overlayTable"])
        base36, base25 = self.word(table + 36 * 32), self.word(table + 25 * 32)
        profile = self.manifest[self.profile_key]
        caller_overlay = profile["boundary_call_overlay"]
        caller = next(s for s in self.manifest["sections"] if s["overlay"] == caller_overlay)
        caller_base = self.word(table + caller_overlay * 32) if caller_overlay else caller["vram"]
        require((state, status, target.value, site.value) == (3, -3, self.boundary_address(), caller_base + profile["boundary_call_offset"]),
                f"Unexpected manager stop: {(state, status, hex(target.value), hex(site.value))}")
        require(created and (thread_state.value, thread_status.value) == (self.expected_audio_state, 0),
                f"Unexpected audio thread state: {(created, thread_state.value, thread_status.value)}")
        return {"function": profile["boundary_function"], "status": status, "target": f"0x{target.value:08X}",
                "call_site": f"0x{site.value:08X}", "overlay36": f"0x{base36:08X}", "overlay25": f"0x{base25:08X}"}


def run(library_path, rom_path, manifest_path, report_path, *, session_class=ManagerSession,
        extra_checks=None, label="audio manager"):
    lib = load_native_library(library_path)
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    cases = []
    for tv in (1, 0, 2):
        session = session_class(lib, rom, manifest, tv, dirty_heap=True)
        try:
            boundary = session.bootstrap()
            assets = check_audio_assets(session, initial_heap_only=False)
            ai = session.ai()
            require(ai["requested"] == 22020 and ai["writes"] == 3 and ai["control"] == 1 and not ai["rejected"], "Original manager did not configure AI")
            queues = {}
            for name, capacity in (("D_800F1B0C_B9ABC", 8), ("D_800F1AF4_B9AA4", 8), ("D_800F2898_BA848", 76)):
                address = session.symbols[name]
                require(session.word(address + 16) == capacity and session.word(address + 8) == 0,
                        f"Audio queue {name} was not initialized empty")
                queues[name] = capacity
            heap_used = session.word(session.symbols["hp"] + 4) - session.word(session.symbols["hp"])
            require(heap_used > 16, "Synthesizer did not allocate its state")
            require(session.word(session.symbols["n_alGlobals"]) == session.symbols["ALGLOBALS"], "Audio globals were not installed")
            require(session.word(session.symbols["n_syn"]) != 0, "Synthesizer state missing")
            frame_counts = {name: session.word(session.symbols[name]) for name in
                            ("D_800F2168_BA118", "D_800F216C_BA11C", "D_800F2170_BA120")}
            transfers = [e for e in session.native.events() if e[0] == 1]
            extra = extra_checks(session) if extra_checks else {}
        finally:
            joined = session.close()
        require(joined == 3 and session.ai()["active"] == 0, "Manager session did not retire its workers and AI state")
        cases.append({"name": f"{session_class.profile_key.removesuffix('_profile')}_creation_tv_{tv}", "status": "passed", "boundary": boundary,
                      "ai": ai, "assets": assets, "queues": queues, "frame_counts": frame_counts,
                      "rom_transfers": len(transfers), "threads_joined": joined, "extra_checks": extra})
        print(f"PASS {label} TV {tv}: {ai['actual']} Hz, {heap_used} heap bytes, {joined} threads joined", flush=True)
    report = {"status": "passed", "platform": platform.platform(), "cases": cases,
              "limits": [("Audio thread started and waiting; no samples or device output" if session_class.expected_audio_state == 1
                          else "Audio thread created but not started; no samples or device output"),
                         f"Stops before {manifest[session_class.profile_key]['boundary_function']}"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report)
