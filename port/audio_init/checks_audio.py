#!/usr/bin/env python3
"""Check the original audio asset preparation before starting the audio manager."""
import argparse
import ctypes
import json
from pathlib import Path
import platform
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_bootstrap.py").exists() else HERE.parent / "bootstrap"))
from checks_bootstrap import BootstrapSession, configure_bootstrap, require, require_success, load_native_library
from asset_checks import check_audio_assets


class AudioSession(BootstrapSession):
    def bootstrap(self):
        slot = self.create("mainInitGame")
        self.start(slot)
        self.count += 1
        state, status, _ = self.drive(slot)
        target, site = ctypes.c_uint32(), ctypes.c_uint32()
        require_success(self.lib.jfg_threads_error(slot, ctypes.byref(target), ctypes.byref(site)), "Read audio manager boundary")
        table = self.word(self.symbols["overlayTable"])
        base36, base25 = self.word(table + 36 * 32), self.word(table + 25 * 32)
        expected = next(f["vram"] for f in self.manifest["functions"] if f["name"] == "amCreateAudioMgr")
        require((state, status, target.value, site.value) == (3, -3, expected, base25 + 0x64C),
                f"Unexpected audio data stop: {(state, status, hex(target.value), hex(site.value))}")
        return {"function": "amCreateAudioMgr", "status": status, "target": f"0x{target.value:08X}",
                "call_site": f"0x{site.value:08X}", "overlay36": f"0x{base36:08X}", "overlay25": f"0x{base25:08X}"}


def run(library_path, rom_path, manifest_path, report_path):
    lib = load_native_library(library_path)
    configure_bootstrap(lib)
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    cases = []
    for tv in (1, 0, 2):
        session = AudioSession(lib, rom, manifest, tv, dirty_heap=True)
        try:
            boundary = session.bootstrap()
            assets = check_audio_assets(session)
            transfers = [e for e in session.native.events() if e[0] == 1]
            io = session.io()
        finally:
            joined = session.close()
        require(joined == 2 and session.io()["pending"] == 0, "Audio setup left workers or DMA")
        cases.append({"name": f"audio_data_tv_{tv}", "status": "passed", "boundary": boundary,
                      "assets": assets, "rom_transfers": len(transfers), "io": io, "threads_joined": joined})
        print(f"PASS audio data TV {tv}: {len(transfers)} ROM reads, {joined} threads joined", flush=True)
    report = {"status": "passed", "platform": platform.platform(), "cases": cases,
              "limits": ["Original audio data preparation only; manager and sample output have not started",
                         "mainInitGame and amInit remain incomplete"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report)
