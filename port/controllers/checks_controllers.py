#!/usr/bin/env python3
"""Run original joyInit with an explicit no-controller test backend."""
import argparse
import ctypes
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE if (HERE / "checks_map.py").exists() else HERE.parent / "audio_map"))
from checks_map import MapSession
from checks_manager import require, require_success, run
from controller_checks import check_controllers

FIELDS = ("active", "initialized", "pending", "reads_started", "si_delivered", "si_dropped", "rejected", "queue")


class ControllerSession(MapSession):
    profile_key = "controller_profile"

    def __init__(self, lib, *args, **kwargs):
        super().__init__(lib, *args, **kwargs)
        for name in ("begin", "end", "pump"):
            func = getattr(lib, "jfg_controllers_" + name)
            func.argtypes, func.restype = (ctypes.POINTER(ctypes.c_uint8),), ctypes.c_int
        lib.jfg_controllers_state.argtypes = (ctypes.POINTER(ctypes.c_uint64), ctypes.c_size_t)
        lib.jfg_controllers_state.restype = ctypes.c_int
        require_success(lib.jfg_controllers_begin(self.native.memory), "Bind absent-controller backend")

    def controllers(self):
        fields = (ctypes.c_uint64 * 8)()
        require_success(self.lib.jfg_controllers_state(fields, 8), "Read controller backend")
        return dict(zip(FIELDS, fields))

    def write(self, address, data):
        for index, value in enumerate(data):
            self.native.memory[((address & 0x1FFFFFFF) + index) ^ 3] = value

    def bootstrap(self):
        result = super().bootstrap()
        state = self.controllers()
        require(state["initialized"] == state["pending"] == state["reads_started"] == 1,
                "joyInit did not initialize and start one logical controller read")
        require(state["si_delivered"] == state["si_dropped"] == 0,
                "Controller read completed before the owner pump")
        return result

    def close(self):
        joined = super().close()
        require(not any(self.controllers().values()), "Controller state survived session cleanup")
        return joined


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("library", "rom", "manifest", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    run(a.library, a.rom, a.manifest, a.report, session_class=ControllerSession,
        extra_checks=check_controllers, label="controller bootstrap (four absent ports)")
