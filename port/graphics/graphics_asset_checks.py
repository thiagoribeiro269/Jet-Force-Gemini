"""Independent checks for the two private US ROM assets in the graphics proof."""
from __future__ import annotations

import hashlib
import struct
import zlib

from asset_checks import _section
from checks_manager import require

MODEL_ID = 35
TEXTURE_ID = 0x9097
RAM_START, RAM_END = 0x80000000, 0x80400000


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def _asset(rom: bytes, lut: bytes, lut_end: int, table_index: int,
           data_index: int, number: int) -> tuple[bytes, int]:
    table, _ = _section(rom, lut, lut_end, table_index)
    data, base = _section(rom, lut, lut_end, data_index)
    require(0 <= number < len(table) // 4 - 1, "Asset index leaves offset table")
    start, end = struct.unpack_from(">II", table, number * 4)
    require(start < end <= len(data) and not (start | end) & 0xF,
            "Asset offsets leave the ROM data section")
    return data[start:end], base + start


def _deflate(asset: bytes, header_offset: int) -> bytes:
    require(len(asset) > header_offset + 5 and asset[header_offset + 4] == 9,
            "Expected original five-byte RZIP header")
    declared = struct.unpack_from("<I", asset, header_offset)[0]
    require(0 < declared <= 0x100000, "RZIP expanded size is not bounded")
    stream = zlib.decompressobj(-15)
    expanded = stream.decompress(asset[header_offset + 5:], declared + 1)
    expanded += stream.flush()
    require(stream.eof and len(expanded) == declared and not stream.unconsumed_tail,
            "Independent raw DEFLATE decode differs from RZIP size")
    return expanded


def _cache(session, table_pointer: int, count: int, key: int) -> int:
    require(0 < count < 0x1000, "Cache count is outside the original table")
    found = []
    for index in range(count):
        entry = session.read(table_pointer + index * 8, 8)
        if _u32(entry, 0) == key:
            found.append(_u32(entry, 4))
    require(len(found) == 1 and RAM_START <= found[0] < RAM_END,
            f"Expected exactly one cache entry for 0x{key:X}")
    return found[0]


def _dlist(session, pointer: int, *, max_commands: int = 512) -> list[tuple[int, int]]:
    require(RAM_START <= pointer < RAM_END and pointer % 8 == 0,
            "Display list pointer leaves aligned RDRAM")
    commands = []
    for index in range(max_commands):
        require(pointer + index * 8 + 8 <= RAM_END,
                "Display list leaves RDRAM")
        command = struct.unpack(">II", session.read(pointer + index * 8, 8))
        commands.append(command)
        if command[0] >> 24 == 0xB8:  # gSPEndDisplayList in this F3DJFG stream
            return commands
    raise AssertionError("Display list has no end opcode within the bounded scan")


def check_graphics_assets(session, result_pointer: int) -> dict:
    """Validate the original loader's RAM effects against ROM bytes.

    This checker deliberately does not claim microcode execution or a picture.
    The mutable header pointers, reference counts, and generated display lists
    are checked by structure; immutable geometry and texels are byte exact.
    """
    symbols, rom = session.symbols, session.rom
    lut_end = symbols["__ASSETS_LUT_END"]
    lut = rom[symbols["__ASSETS_LUT_START"]:lut_end]
    model_compressed, model_rom = _asset(rom, lut, lut_end, 0x26, 0x27, MODEL_ID)
    model_raw = _deflate(model_compressed, 0)
    require(len(model_compressed) == 176 and len(model_raw) == 296 and
            model_rom == 0x013AB070 and model_raw[:7] == b"swdoor\0",
            "Model 35 is not the pinned US ROM asset")
    require(tuple(model_raw[0x10:0x18]) == (1, 0, 0, 4, 0, 2, 0, 1),
            "Model 35 counts changed")
    require(tuple(_u32(model_raw, field) for field in (0x18, 0x20, 0x1C, 0x24)) ==
            (0x88, 0xB0, 0xD0, 0x90), "Model 35 geometry offsets changed")
    require(_u16(model_raw, 0x8E) == TEXTURE_ID,
            "Model 35 no longer references texture 0x9097")
    triangles = model_raw[0xB0:0xD0]
    vertices = model_raw[0xD0:0xF8]
    require(len(triangles) == 2 * 16 and len(vertices) == 4 * 10 and
            [tuple(triangles[offset + j] for j in (1, 2, 3))
             for offset in (0, 16)] == [(0, 1, 2), (0, 2, 3)],
            "Model 35 is no longer the pinned two-triangle quad")

    # The model's ID has bit 15 clear, and its sole texture ID has bit 15 set.
    texture_compressed, texture_rom = _asset(rom, lut, lut_end, 1, 0,
                                             TEXTURE_ID & 0x7FFF)
    texture_raw = _deflate(texture_compressed, 0x20)
    require(len(texture_compressed) == 1888 and len(texture_raw) == 0x1020 and
            texture_rom == 0x00776E10 and
            texture_raw[:3] == bytes((32, 64, 1)) and
            len(texture_raw[0x20:]) == 32 * 64 * 2,
            "Texture 0x9097 is not the pinned 32x64 RGBA16 asset")
    require(texture_compressed[0x19] != 0,
            "Original texture compression branch is no longer selected")

    model_cache = session.word(symbols["D_800F6F14_B1754"])
    model_count = session.word(symbols["D_800F6F1C_B175C"])
    model = _cache(session, model_cache, model_count, MODEL_ID)
    require(RAM_START <= result_pointer < RAM_END and result_pointer != model and
            session.word(result_pointer) == model,
            "flags=0 must return a model instance that points to the cached model")
    require(session.read(model, 0x10) == model_raw[:0x10] and
            session.read(model + 0xB0, len(triangles)) == triangles and
            session.read(model + 0xD0, len(vertices)) == vertices,
            "Original model name or immutable geometry differs from ROM")
    require(session.word(model + 0x18) == model + 0x88 and
            session.word(model + 0x20) == model + 0xB0 and
            session.word(model + 0x1C) == model + 0xD0 and
            session.word(model + 0x24) == model + 0x90,
            "Model header pointers were not relocated to decoded geometry")

    texture_cache = session.word(symbols["D_800FF9C8"])
    texture_count = session.word(symbols["D_800FF9D0"])
    texture = _cache(session, texture_cache, texture_count, TEXTURE_ID)
    require(session.word(model + 0x88) == texture,
            "Model texture table does not point to the loaded texture")
    require(session.read(texture + 0x20, 32 * 64 * 2) == texture_raw[0x20:],
            "Decoded RGBA16 texels differ from the independent DEFLATE result")
    require(session.read(texture, 3) == texture_raw[:3],
            "Texture dimensions or format differ from ROM")
    texture_commands = struct.unpack(">h", session.read(texture + 0xA, 2))[0]
    texture_dlist = session.word(texture + 0xC)
    require(0 < texture_commands <= 64 and
            RAM_START <= texture_dlist <= RAM_END - texture_commands * 8 and
            texture_dlist % 8 == 0,
            "Texture command list pointer/count are invalid")
    tex_commands = [struct.unpack(">II", session.read(texture_dlist + i * 8, 8))
                    for i in range(texture_commands)]
    tex_opcodes = [word >> 24 for word, _ in tex_commands]
    require(0xFD in tex_opcodes and 0xF5 in tex_opcodes,
            "Original texture setup lacks image/tile commands")
    load_block = [command for command in tex_commands if command[0] >> 24 == 0xF3]
    require(len(load_block) == 1 and load_block[0] == (0xF3000000, 0x077FF000),
            "Texture load block differs from the pinned 4096-byte, DXT=0 transfer")

    model_lists = {}
    for field in (0x74, 0x78):
        pointer = session.word(model + field)
        require(pointer != 0, f"Model graphics list +0x{field:X} is absent")
        commands = _dlist(session, pointer)
        opcodes = [word >> 24 for word, _ in commands]
        require(0xB8 == opcodes[-1],
                f"Model graphics list +0x{field:X} lacks its end command")
        polygon = [command for command in commands if command[0] >> 24 == 0x05]
        vertex = [command for command in commands if command[0] >> 24 == 0x04]
        texture_dma = [command for command in commands if command[0] >> 24 == 0x07]
        require(len(polygon) == len(vertex) == len(texture_dma) == 1,
                f"Model graphics list +0x{field:X} must have one geometry batch")
        # G_TRIN encodes (triangle_count - 1) in bits 20..23, a texture
        # enable bit, and triangle_count * 16 in the low halfword. The two
        # generated lists represent the same source batch, not four triangles.
        require(polygon[0][0] == 0x05110020 and
                polygon[0][1] & 0x1FFFFFFF == (model + 0xB0) & 0x1FFFFFFF,
                f"Model graphics list +0x{field:X} does not reference its two triangles")
        # The effective vertex payload is four 10-byte records plus an
        # eight-byte prefix. Its word1 is zero: the caller supplies DMA base.
        require(vertex[0] == (0x04200030, 0),
                f"Model graphics list +0x{field:X} changed vertex DMA contract")
        # G_DMADL reuses the final six commands of the texture setup list.
        require(texture_dma[0][0] == 0x07060030 and
                texture_dma[0][1] & 0x1FFFFFFF ==
                (texture_dlist + 8) & 0x1FFFFFFF,
                f"Model graphics list +0x{field:X} does not reuse texture commands")
        model_lists[f"0x{field:X}"] = {
            "address": f"0x{pointer:08X}", "commands": len(commands),
            "opcodes": [f"0x{opcode:02X}" for opcode in opcodes],
            "polygon_triangles": 2, "vertex_records": 4,
            "vertex_dma_base": "supplied by caller; word1=0",
            "texture_dma_commands": 6,
            "source_batch": "same two triangles in both lists",
            "sha256": hashlib.sha256(session.read(pointer, len(commands) * 8)).hexdigest(),
        }

    return {
        "model": {"id": MODEL_ID, "rom_address": f"0x{model_rom:08X}",
                  "compressed_bytes": len(model_compressed), "decoded_bytes": len(model_raw),
                  "decoded_sha256": hashlib.sha256(model_raw).hexdigest(),
                  "cache_address": f"0x{model:08X}",
                  "instance_address": f"0x{result_pointer:08X}",
                  "vertices": 4, "triangles": 2, "lists": model_lists},
        "texture": {"id": f"0x{TEXTURE_ID:04X}",
                    "rom_address": f"0x{texture_rom:08X}",
                    "compressed_bytes": len(texture_compressed),
                    "decoded_bytes": len(texture_raw),
                    "decoded_sha256": hashlib.sha256(texture_raw).hexdigest(),
                    "cache_address": f"0x{texture:08X}",
                    "width": 32, "height": 64, "format": "RGBA16",
                    "commands": texture_commands,
                    "load_block_dxt": 0,
                    "command_address": f"0x{texture_dlist:08X}",
                    "command_opcodes": [f"0x{opcode:02X}" for opcode in tex_opcodes]},
        "scope": "Original CPU decode and generated RAM commands only; no RSP/RDP or RT64 image",
    }
