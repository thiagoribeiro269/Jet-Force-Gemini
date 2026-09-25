"""Check the object bootstrap against ROM assets and guest-memory invariants."""
from __future__ import annotations

import hashlib
import math
import struct

from asset_checks import _section
from checks_manager import require
from texture_checks import check_textures


RAM_START = 0x80000000
RAM_END = 0x80400000
SLOT_SIZE = 0x14
OBJECT_POOL_DATA_SIZE = 0x19000
OBJECT_POOL_SLOTS = 0x200


def _words(data: bytes) -> tuple[int, ...]:
    require(len(data) % 4 == 0, "Object directory has a partial word")
    return struct.unpack(f">{len(data) // 4}I", data)


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def _swap_adjacent_bits(value: int) -> int:
    return ((value & 0x55) << 1) | ((value & 0xAA) >> 1)


def _object_table_word(word: bytes) -> bytes:
    """Reproduce overlay 34's local packed 2-bit table conversion."""
    a, b, c, d = word
    channels = (
        (a & 0xC0) | ((b & 0xC0) >> 2) | ((c & 0xC0) >> 4) | ((d & 0xC0) >> 6),
        ((a & 0x30) << 2) | (b & 0x30) | ((c & 0x30) >> 2) | ((d & 0x30) >> 4),
        ((a & 0x0C) << 4) | ((b & 0x0C) << 2) | (c & 0x0C) | ((d & 0x0C) >> 2),
        ((a & 0x03) << 6) | ((b & 0x03) << 4) | ((c & 0x03) << 2) | (d & 0x03),
    )
    return bytes(_swap_adjacent_bits(value) & 0xFF for value in channels)


def _explosion_data(source: bytes, offsets: tuple[int, ...], base: int) -> bytes:
    """Reconstruct the writes made by objInitExplosions to asset 0x40."""
    result = bytearray(source)
    for start, end in zip(offsets, offsets[1:]):
        require(end - start >= 0x58, "Explosion definition is shorter than its header")
        pointers = [_u32(source, start + field) for field in range(0xC, 0x2C, 4)]
        for field, relative in zip(range(0xC, 0x2C, 4), pointers):
            require(relative <= end - start,
                    "Explosion definition pointer leaves its asset")
            struct.pack_into(">I", result, start + field, base + start + relative)

        result[start:start + 4] = b"\x00\x4B\x58\x00"
        for count_offset, pointer_index, stride, kind in (
            (0x31, 3, 0x22, b"\x00\x73\x22\x00"),
            (0x32, 7, 0x1C, b"\x00\x80\x1C\x00"),
        ):
            count = source[start + count_offset]
            relative = pointers[pointer_index]
            require(relative + count * stride <= end - start,
                    "Explosion child entries leave their definition")
            for index in range(count):
                item = start + relative + index * stride
                result[item:item + 4] = kind
    return bytes(result)


def check_objects(session) -> dict:
    """Check original object, light, hit and explosion initialization."""
    previous = check_textures(session)
    symbols, rom = session.symbols, session.rom
    lut_end = symbols["__ASSETS_LUT_END"]
    lut = rom[symbols["__ASSETS_LUT_START"]:lut_end]
    require(_u32(lut, 0) >= 0x40, "Object asset sections are absent")
    assets = {index: _section(rom, lut, lut_end, index)[0]
              for index in (0x18, 0x19, 0x2E, 0x30, 0x3F, 0x40)}

    ranges = []

    def allocation(name: str, size: int, *, address: int | None = None) -> int:
        pointer = address if address is not None else session.word(symbols[name])
        require(size > 0 and RAM_START <= pointer <= RAM_END - size and pointer % 16 == 0,
                f"Object allocation {name} is outside the aligned heap")
        for other in ranges:
            require(pointer + size <= other["address"] or other["address"] + other["bytes"] <= pointer,
                    f"Object allocation {name} overlaps {other['name']}")
        ranges.append({"name": name, "address": pointer, "bytes": size})
        return pointer

    # mmAllocRegion creates a 512-slot child pool in a single main-pool block.
    region = allocation("objregion", OBJECT_POOL_DATA_SIZE + OBJECT_POOL_SLOTS * SLOT_SIZE)
    pool = symbols["gMemoryPools"] + 0x10
    require(session.word(symbols["gNumberOfMemoryPools"]) == 1,
            "Object pool was not installed as pool 1")
    require(tuple(session.word(pool + offset) for offset in (0, 4, 8, 12)) ==
            (OBJECT_POOL_SLOTS, 1, region, OBJECT_POOL_DATA_SIZE + OBJECT_POOL_SLOTS * SLOT_SIZE),
            "Object pool metadata differs")
    require(session.word(region) == region + OBJECT_POOL_SLOTS * SLOT_SIZE and
            session.word(region + 4) == OBJECT_POOL_DATA_SIZE and
            session.read(region + 8, 6) == b"\x00\x00\xff\xff\xff\xff",
            "Object pool first free slot differs")
    for index in range(OBJECT_POOL_SLOTS):
        require(_u16(session.read(region + index * SLOT_SIZE + 0xE, 2), 0) == index,
                "Object pool slot indices differ")

    for name, size in (
        ("deletelist", 0x320), ("playerlist", 0x10), ("animplayerlist", 0x40),
        ("objTempBuf", 0x2000), ("ObjList", 0x800), ("NoAddObjList", 0x100),
        ("D_801047E4_B1754", 0x140), ("D_801047E8_B1758", 0x400),
        ("D_801047EC_B175C", 0x400), ("explosionTypeData", len(assets[0x40])),
        ("explosionTypes", 4 * session.word(symbols["explosionNoTypes"])),
    ):
        allocation(name, size)
    require(session.word(symbols["objTempBufSize"]) == 0x2000,
            "Object temporary buffer size differs")

    objindex = assets[0x30]
    require(len(objindex) % 2 == 0 and len(objindex) >= 2,
            "Object index has invalid halfword length")
    entries = struct.unpack(f">{len(objindex) // 2}h", objindex)
    require(entries[-1] != 0, "Object index has unexpected trailing empty entries")
    maximum = len(entries) - 1
    require(session.word(symbols["objindex_max"]) == maximum,
            "Object index maximum differs")
    objindex_ptr = allocation("objindex", len(objindex))
    require(session.read(objindex_ptr, len(objindex)) == objindex,
            "Object index differs from ROM")

    romtab = assets[0x2E]
    romtab_words = _words(romtab)
    require(romtab_words[-1] == 0xFFFFFFFF and len(romtab_words) >= 3 and
            romtab_words[0] == 0 and all(a < b for a, b in zip(romtab_words[:-2], romtab_words[1:-1])),
            "Object definition directory lacks its ordered sentinel")
    max_types = len(romtab_words) - 2
    require(session.word(symbols["MaxTypes"]) == max_types,
            "Object definition count differs")
    romtab_ptr = allocation("RomTab", len(romtab))
    require(session.read(romtab_ptr, len(romtab)) == romtab,
            "Object definition directory differs from ROM")
    allocation("objdeflist", max_types * 4)
    defno = allocation("objdefno", max_types * 2)
    require(session.read(defno, max_types * 2) == bytes(max_types * 2),
            "Object definition indices were not cleared")

    findex = assets[0x19]
    index_words = _words(findex)
    require(0xFFFFFFFF in index_words and index_words[0] == 0,
            "Findex sentinel or start is invalid")
    fmax = index_words.index(0xFFFFFFFF)
    require(fmax >= 7 and all(a <= b for a, b in zip(index_words[:fmax - 1], index_words[1:fmax])) and
            not any(index_words[fmax + 1:]), "Findex bounds or padding differ")
    require(session.word(symbols["Fmax"]) == fmax,
            "Findex count differs")
    findex_ptr = allocation("Findex", len(findex))
    require(session.read(findex_ptr, len(findex)) == findex,
            "Findex differs from ROM")

    ftables = assets[0x18]
    require(len(ftables) % 4 == 0 and index_words[6] <= len(ftables) // 4 and
            index_words[5] <= index_words[6], "Ftables conversion range is invalid")
    expected_tables = bytearray(ftables)
    for index in range(index_words[5], index_words[6]):
        offset = index * 4
        expected_tables[offset:offset + 4] = _object_table_word(ftables[offset:offset + 4])
    ftables_ptr = allocation("Ftables", len(ftables))
    require(session.read(ftables_ptr, len(ftables)) == expected_tables,
            "Ftables differs from the original selective conversion")

    explosion_index = _words(assets[0x3F])
    require(0xFFFFFFFF in explosion_index and explosion_index[0] == 0,
            "Explosion directory sentinel or start is invalid")
    sentinel = explosion_index.index(0xFFFFFFFF)
    offsets = explosion_index[:sentinel]
    require(len(offsets) >= 2 and offsets[-1] <= len(assets[0x40]) and
            all(a < b for a, b in zip(offsets, offsets[1:])) and
            not any(explosion_index[sentinel + 1:]),
            "Explosion definition offsets or padding differ")
    explosion_count = len(offsets) - 1
    require(session.word(symbols["explosionNoTypes"]) == explosion_count,
            "Explosion type count differs")
    data_ptr = session.word(symbols["explosionTypeData"])
    table_ptr = session.word(symbols["explosionTypes"])
    for index, offset in enumerate(offsets[:-1]):
        require(session.word(table_ptr + index * 4) == data_ptr + offset,
                "Explosion type pointer was not relocated")
    expected_explosions = _explosion_data(assets[0x40], offsets, data_ptr)
    require(session.read(data_ptr, len(expected_explosions)) == expected_explosions,
            "Explosion definitions differ from ROM plus initialization")
    polygons = session.read(symbols["swpolygons"], 0x20)
    require(polygons[:4] == b"\x40\x00\x03\x01" and
            polygons[4:8] == b"\x00\x00\x04\x00" and
            polygons[0x10:0x14] == b"\x40\x00\x02\x03" and
            polygons[0x14:0x1C] == b"\x00\x00\x04\x00\x08\x00\x04\x00",
            "Initial explosion polygons differ")

    # Only these objects/buffers are written by resetVars and lightSetObjectLight.
    require(session.read(symbols["D_800F3860"], 1) == b"\x01" and
            session.word(symbols["ObjListCount"]) == 0,
            "Object state was not reset")
    for name in ("D_800F386C", "D_800F3870", "D_800F3908", "D_800F3910",
                 "D_800F391C", "D_800F38AC", "D_800F38B8", "D_800F38BC",
                 "D_800F38C4", "D_800F3948"):
        require(session.word(symbols[name]) == 0, f"Object state {name} was not reset")
    for name in ("D_800F38C0", "D_800F38C2"):
        require(session.read(symbols[name], 2) == b"\x00\x00",
                f"Object state {name} was not reset")
    require(session.word(symbols["obj_olddt"]) == 0x40000000,
            "Object initial delta time differs")

    light = symbols["D_800F65E8"]
    light_data = session.read(light, 0x20)
    direction = struct.unpack_from(">3f", light_data)
    require(all(math.isfinite(component) for component in direction) and
            abs(sum(component * component for component in direction) - 1.0) < 0.02,
            "Default object light direction is invalid")
    require(light_data[0x14:0x18] == b"\x00\x55\x00\x55" and
            session.word(light + 0xC) == 0x4000 and
            session.word(light + 0x10) == 0 and
            session.word(light + 0x18) == session.word(symbols["D_800F65E0"]),
            "Default object light metadata differs")

    return {
        "previous_bootstrap": previous,
        "object_pool": {"address": f"0x{region:08X}", "bytes": OBJECT_POOL_DATA_SIZE + OBJECT_POOL_SLOTS * SLOT_SIZE,
                        "slots": OBJECT_POOL_SLOTS},
        "object_index_entries": len(entries), "object_definition_types": max_types,
        "findex_entries": fmax, "ftables_converted_words": index_words[6] - index_words[5],
        "explosion_types": explosion_count,
        "asset_sha256": {f"0x{index:02X}": hashlib.sha256(data).hexdigest()
                         for index, data in assets.items()},
        "allocations": [{**item, "address": f"0x{item['address']:08X}"} for item in ranges],
        "default_light_direction": list(direction),
        "scope": "Object, hit, light and explosion initialization; no object instance or rendering",
    }
