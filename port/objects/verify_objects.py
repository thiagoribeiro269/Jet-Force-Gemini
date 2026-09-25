#!/usr/bin/env python3
"""Compare object initialization and both overlay retirements with original MIPS."""
import argparse
import json
from pathlib import Path
import sys

from unicorn import mips_const

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "controllers"))
from verify_controllers import ControllerOracle
from verify_start import run
from checks_objects import ObjectsSession
from checks_manager import require


class ObjectsOracle(ControllerOracle):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.object_overlay_loads = {}

    def platform(self, cpu, pc, size, name):
        if name == "osPiStartDma":
            source = cpu.reg_read(mips_const.UC_MIPS_REG_7) & 0xFFFFFFFF
            for overlay in (33, 34):
                section = next(s for s in self.manifest["sections"] if s["overlay"] == overlay)
                if source == section["rom"]:
                    require(overlay not in self.object_overlay_loads, "Unexpected repeated object overlay load")
                    sp = cpu.reg_read(mips_const.UC_MIPS_REG_SP) & 0xFFFFFFFF
                    self.object_overlay_loads[overlay] = self.u32(sp + 0x10)
        super().platform(cpu, pc, size, name)

    def stop_at_audio(self, cpu, pc, size, data):
        table = self.u32(self.manifest["reference_symbols"]["overlayTable"])
        base36 = self.u32(table + 36 * 32)
        require(set(self.object_overlay_loads) == {33, 34}, "Reference skipped an object overlay load")
        require(self.traps == [0x80044EF8, base36 + 0x14, base36 + 0x60,
                               self.object_overlay_loads[34] + 0x2A8],
                "Reference object initialization followed an unexpected lazy-load path")
        require(all(self.u32(table + overlay * 32) == 0 for overlay in (33, 34)),
                "Reference did not retire both temporary object overlays")
        super().stop_at_audio(cpu, pc, size, data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    report = run(args.elf, args.rom, args.manifest, args.library, args.report,
                 session_class=ObjectsSession, boundary_name="explosionFlushBlasts",
                 compare_a0=False, oracle_class=ObjectsOracle)
    report["limits"].insert(0, "Controller discovery/read retain the explicit absent-port contracts; no PIF DMA, physical input or boot timer proof")
    report["limits"].insert(1, "Object initialization, lighting RAM and transformed data are compared; no general trigonometric/FPR/FCSR equivalence claim")
    report["limits"].insert(2, "Stops before explosionFlushBlasts after original unloads of overlays 33 and 34; no frame, rendering or gameplay")
    args.report.write_text(json.dumps(report, indent=2) + "\n")
