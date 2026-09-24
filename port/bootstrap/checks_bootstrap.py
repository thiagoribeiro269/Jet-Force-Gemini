#!/usr/bin/env python3
"""Native diagnostic for the game's real lazy overlay loader."""
import argparse
import ctypes
import json
from pathlib import Path
import platform
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE if (HERE / "checks_init.py").exists() else HERE.parent / "init"))
from checks_init import InitSession, configure_init, require, require_success, load_native_library


def configure_bootstrap(lib):
    configure_init(lib)
    lib.jfg_poc_watch_game_linker.argtypes = lib.jfg_poc_bind_game_linker.argtypes
    lib.jfg_poc_watch_game_linker.restype = ctypes.c_int


class BootstrapSession(InitSession):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        require_success(self.lib.jfg_poc_watch_game_linker(self.native.memory, len(self.native.memory),
            self.symbols["overlayTable"], self.symbols["overlayCount"]), "Watch original linker")

    def bootstrap(self):
        slot = self.create("mainInitGame")
        self.start(slot)
        self.count += 1
        state, status, _ = self.drive(slot)
        target, site = ctypes.c_uint32(), ctypes.c_uint32()
        require_success(self.lib.jfg_threads_error(slot, ctypes.byref(target), ctypes.byref(site)), "Read audio boundary")
        table = self.word(self.symbols["overlayTable"])
        base36, base25 = self.word(table + 36 * 32), self.word(table + 25 * 32)
        require(base36 != 0 and base25 != 0, "Original loader did not load bootstrap/audio overlays")
        require((state, status, target.value, site.value) == (3, -3, base25 + 0x308, base36 + 0x14),
                f"Unexpected bootstrap stop: {(state, status, hex(target.value), hex(site.value))}")
        require(self.lib.jfg_poc_address(b"mainInitRlo") == base36, "Dynamic bootstrap base is not registered")
        # The real loader must have repatched both the main and overlay calls.
        for address, expected in ((0x80044EF8, base36), (base36 + 0x14, base25 + 0x308)):
            word = self.word(address)
            require(word >> 26 == 3 and (((address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)) == expected,
                    "Original runLink did not patch its JAL")
        return {"function": "amInit", "status": status, "target": f"0x{target.value:08X}",
                "call_site": f"0x{site.value:08X}", "overlay36": f"0x{base36:08X}", "overlay25": f"0x{base25:08X}"}


def run(library_path, rom_path, manifest_path, report_path):
    lib = load_native_library(library_path)
    configure_bootstrap(lib)
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    cases = []
    for tv in (1, 0, 2):
        s = BootstrapSession(lib, rom, manifest, tv, dirty_heap=True)
        boundary = s.bootstrap()
        transfers = [e for e in s.native.events() if e[0] == 1]
        joined = s.close()
        require(joined == 2 and s.io()["pending"] == 0, "Bootstrap workers or DMA survived cleanup")
        cases.append({"name": f"dynamic_bootstrap_tv_{tv}", "status": "passed", "boundary": boundary,
                      "rom_transfers": len(transfers), "threads_joined": joined})
        print(f"PASS dynamic bootstrap TV {tv}: {boundary['function']}, {len(transfers)} ROM reads", flush=True)
    report = {"status": "passed", "platform": platform.platform(), "cases": cases,
              "limits": ["Stops before original amInit body; no audio or graphics output", "Bootstrap is not complete"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--library", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report)
