#!/usr/bin/env python3
"""Compare texture/model initialization with the declared controller fixture."""
import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "controllers"))
from verify_controllers import ControllerOracle
from verify_start import run
from checks_textures import TexturesSession
from checks_manager import require


class TexturesOracle(ControllerOracle):
    def stop_at_audio(self, cpu, pc, size, data):
        table = self.u32(self.manifest["reference_symbols"]["overlayTable"])
        base36, base34 = self.u32(table + 36 * 32), self.u32(table + 34 * 32)
        require((pc & 0xFFFFFFFF) == base34 and
                self.traps == [0x80044EF8, base36 + 0x14, base36 + 0x60],
                "Original object overlay did not follow the expected lazy-load path")
        super().stop_at_audio(cpu, pc, size, data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    # mainPreNMI runs after modInitModels. Its final libultra queue poll
    # leaves a0=1, while the native import preserves resetMsgQueue. Keep the
    # earlier exact-value check instead of claiming all caller-saved GPRs match.
    report = run(args.elf, args.rom, args.manifest, args.library, args.report,
                 session_class=TexturesSession, boundary_name="objInitObjects",
                 compare_a0=False, oracle_class=TexturesOracle)
    report["limits"].insert(0, "Controller discovery/read use the explicit four-absent-port API contracts; original PIF DMA, boot timer and private SI state are not executed")
    report["limits"].insert(1, "This MIPS comparison checks texture/model directories and allocations before object initialization and SI completion; the separate native state check pumps SI before auditing those directories")
    report["limits"].insert(2, "No individual texture, sprite or model is decoded or rendered")
    args.report.write_text(json.dumps(report, indent=2) + "\n")
