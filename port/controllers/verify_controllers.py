#!/usr/bin/env python3
"""Compare controller bootstrap under explicit high-level absent-port contracts."""
import argparse
import json
from pathlib import Path
import sys
from unicorn import mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/audio_start"))
from verify_start import StartOracle, run, sx32
from checks_controllers import ControllerSession
from checks_manager import require
from controller_reference import compare_status_decoder


class ControllerOracle(StartOracle):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.controller_initialized = False
        self.controller_pending = False
        self.controller_queue = None

    def platform(self, cpu, pc, size, name):
        if name in ("osContInit", "osContStartReadData"):
            reg = lambda i: cpu.reg_read(getattr(mips_const, f"UC_MIPS_REG_{i}")) & 0xFFFFFFFF
            queue = reg(4)
            require(queue in self.queues, "Controller oracle received an uninitialized queue")
            if name == "osContInit" and not self.controller_initialized:
                self.write(reg(5), b"\x00")
                for i in range(4): self.write(reg(6) + i * 4 + 3, b"\x08")
                self.controller_initialized, self.controller_queue = True, queue
            elif name == "osContStartReadData":
                require(self.controller_initialized and not self.controller_pending and queue == self.controller_queue,
                        "Controller oracle encountered an unsupported overlapping read")
                self.controller_pending = True
            cpu.reg_write(mips_const.UC_MIPS_REG_2, 0)
            cpu.reg_write(mips_const.UC_MIPS_REG_PC, cpu.reg_read(mips_const.UC_MIPS_REG_RA))
            return
        super().platform(cpu, pc, size, name)

    def stop_at_audio(self, cpu, pc, size, data):
        require(self.controller_initialized and self.controller_pending, "Original joyInit skipped discovery or the read")
        super().stop_at_audio(cpu, pc, size, data)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    decoder = compare_status_decoder(a.library, a.rom, a.manifest)
    report = run(a.elf, a.rom, a.manifest, a.library, a.report,
                 session_class=ControllerSession, boundary_name="texInitTextures", compare_a0=True,
                 oracle_class=ControllerOracle)
    report["controller_status_decoder"] = decoder
    report["limits"].insert(0, "Controller APIs use declared high-level absent-port hooks in this bootstrap oracle; original osContInit/StartReadData PIF DMA, private kernel state and half-second delay are not executed")
    report["limits"].insert(1, "RAM equality is under those API contracts and before the native SI completion pump; it is not full libultra/SI hardware equivalence")
    a.report.write_text(json.dumps(report, indent=2) + "\n")
