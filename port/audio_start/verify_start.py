#!/usr/bin/env python3
"""Compare startup through amInitAudioMap and the audio thread's first wait."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_CODE, mips_const

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/audio_manager"))
from verify_manager import ManagerOracle, masked, HOST, SLOTS, unique_symbol, sx32, RETURN
from checks_manager import require, load_native_library
from checks_start import StartSession


MAIN_GPRS = (0, 5, 6, 7, *range(16, 24), 28, 29, 30, 31)
AUDIO_ENTRY = 0x80001D7C
RECV_SITE = 0x80001DFC


class StartOracle(ManagerOracle):
    def __init__(self, image, rom, manifest, symbols, boundary_name="amInitAudioMap"):
        super().__init__(image, rom, manifest, symbols, boundary_name=boundary_name)
        self.first_wait = None
        self.audio_phase = False
        recv = sx32(symbols["osRecvMesg"])
        self.cpu.hook_add(UC_HOOK_CODE, self.stop_at_first_wait, begin=recv, end=recv)

    def stop_at_first_wait(self, cpu, pc, size, data):
        if not self.audio_phase:
            return
        require(self.first_wait is None, "Audio thread attempted another receive")
        reg = lambda number: cpu.reg_read(getattr(mips_const, f"UC_MIPS_REG_{number}")) & 0xFFFFFFFF
        queue, output, flags, sp = (reg(4), reg(5), reg(6), reg(29))
        call_site = (reg(31) - 8) & 0xFFFFFFFF
        symbols = self.manifest["reference_symbols"]
        expected_queue = symbols["D_800F1AF4_B9AA4"]
        stack_top = symbols["D_800F17B8_B9768"]
        require((pc & 0xFFFFFFFF, call_site) == (self.symbols["osRecvMesg"], RECV_SITE),
                "Reference did not reach the first audio receive")
        require((queue, output, flags, sp) == (expected_queue, sp + 0x44, 1, stack_top - 0x58),
                "Reference audio receive arguments or stack delta differ")
        require(self.u32(queue + 8) == 0 and self.u32(queue + 16) == 8,
                "Reference audio queue was not empty at the first blocking receive")
        require(self.u32(output) == 0 and self.u32(sp + 0x40) == 0,
                "Reference audio message slots were not initialized")
        self.first_wait = {"call_site": f"0x{call_site:08X}", "queue": f"0x{queue:08X}",
                           "message_slot": f"0x{output:08X}", "flags": flags,
                           "stack_pointer": f"0x{sp:08X}", "entry_to_wait_stack_delta": -0x48}
        # This oracle checks the serial CPU prefix. It does not enter the
        # blocking libultra receive or pretend to schedule a retrace/frame.
        cpu.reg_write(mips_const.UC_MIPS_REG_PC, sx32(RETURN))


def run(elf_path, rom_path, manifest_path, library_path, report_path, *,
        session_class=StartSession, boundary_name="amInitAudioMap", compare_a0=False,
        oracle_class=StartOracle):
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    profile = manifest[session_class.profile_key]
    require(profile["boundary_function"] == boundary_name and
            profile["thread_function"] == "__amMain", "Unexpected audio-start profile")
    with elf_path.open("rb") as file:
        syms = ELFFile(file).get_section_by_name(".symtab")
        symbols = {name: unique_symbol(syms, name)["st_value"] for name in
                   ("__osDisableInt", "__osRestoreInt", "TrapDanglingJump",
                    "osCreateMesgQueue", "osRecvMesg", "mainInitGame", "__amMain")}
    require(symbols["__amMain"] == AUDIO_ENTRY, "Original audio entry moved")
    lib = load_native_library(library_path)
    cases = []
    for tv in (1, 0, 2):
        session = session_class(lib, rom, manifest, tv, dirty_heap=True)
        try:
            # The oracle owns the image from before the native main thread is
            # started. Never seed it from a native snapshot after audio starts:
            # osScAddClient would then insert its client a second time.
            oracle = oracle_class(session.native.snapshot(), rom, manifest, symbols, boundary_name=boundary_name)
            registers = [0] * 32
            registers[29], registers[31] = sx32(0x80780000 - 0x10), sx32(RETURN)
            oracle.call(symbols["mainInitGame"], registers, budget=18000000)
            require(oracle.boundary is not None and oracle.boundary_registers is not None,
                    "Original main thread did not reach the audio-map boundary")
            main_boundary = dict(oracle.boundary)
            main_registers = list(oracle.boundary_registers)

            # The real console schedules this child independently. Running its
            # prefix second on the same MIPS RAM proves CPU effects, not timing.
            child = [0] * 32
            child[29] = sx32(manifest["reference_symbols"]["D_800F17B8_B9768"] - 0x10)
            child[31] = sx32(RETURN)
            oracle.audio_phase = True
            oracle.call(symbols["__amMain"], child, budget=200000)
            require(oracle.first_wait is not None, "Original audio thread did not reach its first wait")

            boundary = session.bootstrap()
            require(main_boundary == {"target": int(boundary["target"], 16),
                                      "call_site": int(boundary["call_site"], 16)},
                    "Original main audio-map boundary differs")
            require(main_boundary["target"] == session.boundary_address() and
                    main_boundary["call_site"] == int(boundary["overlay36"], 16) + profile["boundary_call_offset"],
                    "Audio-map boundary address or call site differs")
            actual_registers = session.poll(SLOTS)[2]
            compared_gprs = tuple(sorted((*MAIN_GPRS, 4))) if compare_a0 else MAIN_GPRS
            gpr_differences = {i: [f"0x{main_registers[i]:016X}", f"0x{actual_registers[i]:016X}"]
                               for i in compared_gprs if actual_registers[i] != main_registers[i]}
            # The final mainPreNMI polls resetMsgQueue. Original libultra's
            # osRecvMesg leaves a0 holding the interrupt mask (1), while its
            # native import preserves the queue argument. The following
            # amInitAudioMap entry takes no arguments and overwrites a0.
            if not compare_a0:
                reset_queue = session.symbols["resetMsgQueue"]
                require(main_registers[4] == 1 and actual_registers[4] == sx32(reset_queue),
                        "Unexpected a0 values after the reset-queue poll")
            ai = session.ai()
            ai_writes = [[ai["write0"], ai["dac"]], [ai["write1"], ai["bitrate"]],
                         [ai["write2"], ai["control"]]]
            require(oracle.audio_writes == ai_writes and ai["writes"] == 3,
                    "Original AI configuration writes differ")
            transfers = [[event[1], event[2], event[3]]
                         for event in session.native.events() if event[0] == 1]
            require(transfers == oracle.transfers, "Original ROM transfers differ")
            require(oracle.traps[:2] == [0x80044EF8, int(boundary["overlay36"], 16) + 0x14],
                    "Original lazy-load path differs")

            queue = session.symbols["D_800F1AF4_B9AA4"]
            client = session.symbols["D_800E97A8_B1758"]
            require(session.word(queue + 8) == 0 and session.word(queue + 16) == 8,
                    "Native audio queue is not empty at first wait")
            require(session.word(client + 8) == queue,
                    "Native audio scheduler client points to the wrong queue")
            regions = [(HOST, 0x100), (SLOTS, 0x200),
                       (session.symbols["sc"] + 0xB0, 0x1B0),
                       (session.symbols["Time"] - 0x400, 0x420),
                       (0x80780000 - 0x10 - 0x2000, 0x2040),
                       (session.symbols["D_800F17C0_B9770"], 0x1B0)]
            regions += [(q, 8) for q in sorted(oracle.queues)]
            expected = masked(oracle.memory(), regions)
            actual = masked(session.native.snapshot(), regions)
            if gpr_differences:
                ram_difference = next((i for i, (left, right) in enumerate(zip(expected, actual))
                                       if left != right), None)
                raise AssertionError(f"Main thread preserved GPRs differ (MIPS, native): "
                                     f"{gpr_differences}; first masked RAM difference: "
                                     f"{None if ram_difference is None else f'0x{ram_difference:08X}'}")
            if expected != actual:
                offset = next(i for i, (left, right) in enumerate(zip(expected, actual)) if left != right)
                raise AssertionError(f"Audio-start RAM differs at {offset:08X}: "
                                     f"{expected[offset:offset+16].hex()} != {actual[offset:offset+16].hex()}")
            cases.append({"name": f"original_{session_class.profile_key.removesuffix('_profile')}_tv_{tv}", "status": "passed",
                          "boundary": boundary, "first_wait": oracle.first_wait,
                          "compared_gprs": list(compared_gprs), "rom_transfers": len(transfers),
                          "caller_saved_a0": {"mips": f"0x{main_registers[4]:016X}",
                                              "native": f"0x{actual_registers[4]:016X}"},
                          "ai_writes": ai_writes,
                          "masked_regions": [[f"0x{address:08X}", size] for address, size in regions],
                          "ram_sha256": hashlib.sha256(actual).hexdigest()})
            print(f"PASS MIPS {session_class.profile_key} TV {tv}", flush=True)
        finally:
            joined = session.close()
        cases[-1]["threads_joined"] = joined
    report = {"status": "passed", "cases": cases, "limits": [
        "The MIPS oracle executes main then the audio-thread prefix serially on one original RAM image; this is not a timing or scheduling proof",
        "Stops before the first empty blocking osRecvMesg executes; no retrace or audio frame is simulated",
        "Private kernel structures, queue wait lists and prior main-thread call stacks are excluded; the new audio frame and scheduler client are compared",
        ("Explicit platform hooks retained; 17 main-thread GPRs compared directly" if compare_a0 else
         "Explicit platform hooks retained; 16 main-thread GPRs compared; a0 after the reset-queue poll is recorded separately because the native queue import preserves the argument while libultra leaves the interrupt mask"),
        "No general FPU/FCSR proof"]}
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("elf", "rom", "manifest", "library", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    run(args.elf, args.rom, args.manifest, args.library, args.report)
