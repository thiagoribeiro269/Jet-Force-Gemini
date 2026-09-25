#!/usr/bin/env python3
"""Load the private Boy asset, prove its CPU load and export a static-pose fixture."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/graphics"))
from verify_graphics import (GraphicsSession, InitOracle, ELFFile, unique_symbol,
                             load_native_library, check_objects, masked, HOST,
                             SLOTS, sx32, RETURN, require)
from graphics_asset_checks import _asset, _deflate, _dlist


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare_rest_pose(ram, rom, manifest, symbols, model, matrices):
    """Assemble zero-rotation bone translations and check original gen_anim_data."""
    bone_base = struct.unpack_from(">I", ram, (model & 0x1FFFFFFF) + 0x54)[0]
    world, host_matrices = [], bytearray()
    f32 = lambda value: struct.unpack(">f", struct.pack(">f", value))[0]
    for index in range(21):
        at = (bone_base & 0x1FFFFFFF) + index * 16
        parent, slot, rotation0, rotation1 = struct.unpack_from(">bBBB", ram, at)
        require(-1 <= parent < index and (parent == -1) == (index == 0) and
                (slot, rotation0, rotation1) == (index,) * 3, "Unsupported skeleton topology")
        local = struct.unpack_from(">3f", ram, at + 4)
        require(all(math.isfinite(x) and abs(x) < 1024 for x in local), "Invalid bone translation")
        translation = tuple(f32(x + (world[parent][k] if parent >= 0 else 0.0)) for k, x in enumerate(local))
        world.append(translation)
        matrix = [1.0 if k % 5 == 0 else 0.0 for k in range(16)]
        matrix[12:15] = translation
        host_matrices.extend(struct.pack(">16f", *matrix))

    # A deliberately neutral descriptor: no animated rotation/scale or root
    # motion. It exercises the original hierarchy evaluation, not gameplay.
    oracle = InitOracle(ram, rom, manifest, symbols)
    arena, stack = 0x80600000, 0x80610000
    oracle.write(arena, bytes(0x1000))
    oracle.put(arena, matrices)
    oracle.write(arena + 0x100, struct.pack(">16f", *(1.0 if k % 5 == 0 else 0.0 for k in range(16))))
    oracle.put(arena + 0x240, arena + 0x300)
    oracle.write(arena + 0x300, b"\x00\x15" + bytes(254))
    oracle.put(stack + 0x10, 21)
    oracle.put(stack + 0x14, arena + 0x500)
    oracle.write(arena + 0x500, b"\xff")
    registers = [0] * 32
    registers[4:8] = map(sx32, (arena, arena + 0x100, arena + 0x200, bone_base))
    registers[29], registers[31] = sx32(stack), sx32(RETURN)
    oracle.call(symbols["gen_anim_data"], registers, budget=1000000)
    require(oracle.boundary is None and not oracle.transfers, "Unexpected dependency in neutral pose reference")
    start = matrices & 0x1FFFFFFF
    reference = oracle.memory()[start:start + len(host_matrices)]
    require(reference == host_matrices, "Neutral pose matrices differ from original MIPS")
    posed = bytearray(ram)
    posed[start:start + len(host_matrices)] = host_matrices
    return bytes(posed), {"status": "passed", "bone_count": 21, "matrix_bytes": len(host_matrices),
                          "matrix_sha256": digest(host_matrices), "bone_table": bone_base,
                          "method": "host_float32_hierarchy_sum_compared_byte_exact_to_MIPS_gen_anim_data",
                          "reference_inputs": "zero_rotation_descriptor_identity_global_transform_no_root_motion",
                          "changed_range": [matrices, len(host_matrices)], "animated": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-character")
    args = parser.parse_args()
    out = args.out.resolve()
    require(out.is_relative_to(ROOT / "build"), "Private export must remain under ignored build/")
    out.mkdir(parents=True, exist_ok=True)
    rom = (ROOT / "baseroms/baserom.us.z64").read_bytes()
    require(hashlib.sha1(rom).hexdigest() == "493ced9008dbe932d6e91179b68e8630cf23a023", "Wrong US ROM")
    manifest = json.loads((ROOT / "build/port-graphics/proof/manifest.json").read_text())
    library = ROOT / "build/port-graphics/native/libjfg_poc.so"
    with (ROOT / "build/jfg.us.elf").open("rb") as stream:
        table = ELFFile(stream).get_section_by_name(".symtab")
        symbols = {name: unique_symbol(table, name)["st_value"] for name in
                   ("__osDisableInt", "__osRestoreInt", "TrapDanglingJump", "osCreateMesgQueue", "modLoadModel", "gen_anim_data")}
    session = GraphicsSession(load_native_library(library), rom, manifest, 1, dirty_heap=True)
    try:
        session.bootstrap()
        check_objects(session)
        initial = session.native.snapshot()
        oracle = InitOracle(initial, rom, manifest, symbols)
        oracle.command_queue = session.symbols["gPIMesgQueue"]
        index = session.count
        stack = 0x80780000 - index * 0x2000
        registers = [0] * 32
        registers[4], registers[5] = 220, 0
        registers[29], registers[31] = sx32(stack - 0x10), sx32(RETURN)
        print("Comparing original MIPS modLoadModel(220, 0) from validated native pre-load RAM", flush=True)
        expected_regs = oracle.call(symbols["modLoadModel"], registers, budget=100000000)
        require(oracle.boundary is None, "Unexpected deferred function in character load")
        before = len([e for e in session.native.events() if e[0] == 1])
        actual_regs = session.run_function("modLoadModel", 220, 0)
        compared = (0, 2, *range(16, 24), 28, 29, 30, 31)
        require(all(expected_regs[i] == actual_regs[i] for i in compared), "Character return/callee-saved GPRs differ")
        transfers = [[e[1], e[2], e[3]] for e in session.native.events() if e[0] == 1][before:]
        require(transfers == oracle.transfers, "Character ROM transfer trace differs")
        regions = [(HOST, 0x100), (SLOTS + index * 0x200, 0x200),
                   (stack - 0x2000, 0x2040), (session.symbols["gDmaMesgQueue"], 8)]
        ram = session.native.snapshot()
        expected, actual = masked(oracle.memory(), regions), masked(ram, regions)
        require(expected == actual, "Character game RAM differs from MIPS reference")
        instance = actual_regs[2] & 0xFFFFFFFF
        model = session.word(instance)
        lut_end = session.symbols["__ASSETS_LUT_END"]
        lut = rom[session.symbols["__ASSETS_LUT_START"]:lut_end]
        packed, source = _asset(rom, lut, lut_end, 0x26, 0x27, 220)
        raw = _deflate(packed, 0)
        require(raw[:4] == b"Boy\0" and len(raw) == 20848, "Wrong character model")
        counts = struct.unpack_from(">HHH", raw, 0x12)
        require(counts == (660, 520, 82) and raw[0x10] == 18, "Character asset counts differ")
        geometry = []
        for field, length in ((0x1C, 660 * 10), (0x20, 520 * 16)):
            offset = struct.unpack_from(">I", raw, field)[0]
            address = session.word(model + field)
            require(address == model + offset and session.read(address, length) == raw[offset:offset + length],
                    "Character immutable geometry differs from ROM")
            geometry.append({"field": field, "bytes": length, "sha256": digest(raw[offset:offset + length])})
        require(session.word(instance + 4) == session.word(model + 0x1C), "Instance vertex DMA base differs")
        matrices = session.word(instance + 0x10)
        identity = struct.pack(">16f", *(1.0 if i % 5 == 0 else 0.0 for i in range(16)))
        require(session.read(model + 0x4F, 1) == b"\x15" and
                session.read(matrices, 21 * 64) == identity * 21, "Initial pose is not 21 identity float matrices")
        textures = []
        table_offset = struct.unpack_from(">I", raw, 0x18)[0]
        for i in range(18):
            entry = table_offset + i * 8
            tex_id = struct.unpack_from(">H", raw, entry + 6)[0]
            table_id, data_id = (1, 0) if tex_id & 0x8000 else (3, 2)
            packed_tex, tex_rom = _asset(rom, lut, lut_end, table_id, data_id, tex_id & 0x7FFF)
            decoded = _deflate(packed_tex, 0x20) if packed_tex[0x19] else packed_tex
            address = session.word(model + entry)
            require(session.read(address, 3) == decoded[:3], "Texture format/dimensions differ")
            require(session.read(address + 0x20, len(decoded) - 0x20) == decoded[0x20:], "Character texels differ from ROM")
            textures.append({"id": tex_id, "rom": tex_rom, "address": address,
                             "width": decoded[0], "height": decoded[1], "format": decoded[2],
                             "payload_bytes": len(decoded) - 0x20, "payload_sha256": digest(decoded[0x20:])})
        listing = session.word(model + 0x74)
        commands = _dlist(session, listing, max_commands=1024)
        require(len(commands) == 353 and sum(((a >> 20) & 15) + 1 for a, b in commands if a >> 24 == 5) == 502,
                "Original character display list counts differ")
        posed_ram, pose = prepare_rest_pose(ram, rom, manifest, symbols, model, matrices)
        (out / "load-ram.bin").write_bytes(ram)
        (out / "ram.bin").write_bytes(posed_ram)
        frame = {"status": "prepared", "ram": str(out / "ram.bin"), "ram_sha256": digest(posed_ram),
                 "model": 220, "model_name": "Boy", "instance": instance, "cache_address": model,
                 "list_address": listing, "vertex_base": session.word(instance + 4), "matrix_base": matrices,
                 "width": 320, "height": 240, "camera": "controlled_orthographic_character",
                 "original_game_camera": False, "pose": "neutral_bone_hierarchy_MIPS_checked", "animated": False,
                 "display_list_field": "0x74", "triangles_stored": 520, "triangles_submitted": 502}
        (out / "frame-input.json").write_text(json.dumps(frame, indent=2) + "\n")
        report = {"status": "passed", "model": 220, "name": "Boy", "rom_offset": source,
                  "geometry": geometry, "textures": textures, "display_commands": len(commands),
                  "rom_transfers": len(transfers), "compared_gprs": list(compared),
                  "initial_ram_sha256": digest(initial), "compared_ram_sha256": digest(actual),
                  "load_ram_sha256": digest(ram), "frame_ram_sha256": digest(posed_ram), "masked_regions": regions,
                  "pose": pose,
                  "limits": ["NTSC character load only; starts from previously validated native bootstrap RAM",
                             "Original MIPS CPU routines with declared host contracts; no RSP/RDP reference",
                             "Only the declared four kernel/stack/wait regions masked; game allocations compared",
                             "Neutral pose from original skeleton, separately MIPS-checked; controlled camera, no animation or original lighting"]}
    finally:
        joined = session.close()
    require(joined == 4, "Character diagnostic leaked a worker")
    report["threads_joined"] = joined
    (out / "source-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "model", "display_commands", "rom_transfers", "threads_joined")}))


if __name__ == "__main__":
    main()
