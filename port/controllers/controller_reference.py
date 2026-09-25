"""Use the original MIPS status decoder to check absent-port discovery outputs."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/boot"))
from reference import Oracle, sx32, RETURN
from checks_controllers import ControllerSession
from checks_manager import require, load_native_library

QUEUE, BUFFER, PATTERN, STATUS = 0x807B0000, 0x807B1000, 0x807B4000, 0x807B4020


def compare_status_decoder(library_path, rom_path, manifest_path):
    lib = load_native_library(library_path)
    manifest, rom = json.loads(manifest_path.read_text()), rom_path.read_bytes()
    symbols = manifest["reference_symbols"]
    cases = []
    for seed in (bytes(16), bytes(range(16)), b"\xFF" * 16):
        session = ControllerSession(lib, rom, manifest)
        try:
            session.queue(QUEUE, BUFFER, 1)
            session.write(PATTERN, b"\xFF")
            session.write(STATUS, seed)
            oracle = Oracle(session.native.snapshot())
            # __OSContRequesFormat is eight bytes per port. Its rxsize high
            # bit represents the explicit no-response fixture; no physical
            # PIF DMA or osContInit timer is emulated here.
            packet = bytes([0xFF, 1, 0x83, 0, 0xFF, 0xFF, 0xFF, 0xFF])
            oracle.write(symbols["__osContPifRam"], packet * 4 + bytes(32))
            oracle.write(symbols["__osMaxControllers"], b"\x04")
            registers = [0] * 32
            registers[4], registers[5] = sx32(PATTERN), sx32(STATUS)
            registers[29], registers[31] = sx32(0x80790000), sx32(RETURN)
            oracle.call(symbols["__osContGetInitData"], registers, budget=10000)
            image = oracle.memory()
            expected_pattern = image[PATTERN & 0x1FFFFFFF:(PATTERN & 0x1FFFFFFF) + 1]
            expected_status = image[STATUS & 0x1FFFFFFF:(STATUS & 0x1FFFFFFF) + 16]
            require(session.call("osContInit", QUEUE, PATTERN, STATUS)[2] == 0, "Native discovery failed")
            require(session.read(PATTERN, 1) == expected_pattern and session.read(STATUS, 16) == expected_status,
                    "Native absence result differs from the original PIF status decoder")
            cases.append({"initial_status": seed.hex(), "pattern": expected_pattern.hex(),
                          "decoded_status": expected_status.hex(), "status": "passed"})
        finally:
            session.close()
    return {"status": "passed", "cases": cases,
            "scope": "Original __osContGetInitData on four synthetic no-response packets; public outputs only"}
