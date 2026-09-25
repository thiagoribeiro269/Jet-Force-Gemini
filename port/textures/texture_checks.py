"""Check texture/model directories against private ROM data and guest RAM."""
import hashlib
import struct

from asset_checks import _section
from controller_checks import check_controllers
from checks_manager import require


def check_textures(session):
    previous = check_controllers(session)
    symbols, rom = session.symbols, session.rom
    lut_end = symbols["__ASSETS_LUT_END"]
    lut = rom[symbols["__ASSETS_LUT_START"]:lut_end]
    ranges = []

    def allocation(name, address, size):
        require(0x80000000 <= address <= 0x80400000 - size and address % 16 == 0,
                f"Texture allocation {name} outside the original aligned heap")
        for other in ranges:
            require(address + size <= other["address"] or other["address"] + other["bytes"] <= address,
                    f"Texture allocation {name} overlaps {other['name']}")
        ranges.append({"name": name, "address": address, "bytes": size})

    for name, size in (("D_800FF9C8", 0x15E0), ("D_800FF9CC", 0x280),
                       ("D_800FF9EC", 0x320), ("D_800FF9F0", 0x200),
                       ("D_800FFA0C", 0x28),
                       ("D_800F6F14_B1754", 0x2A8), ("D_800F6F18_B1758", 0x190),
                       ("D_800F6F58_B1798", 0x2000), ("D_800F6F28_B1768", 0xA0),
                       ("D_800F6F30_B1770", 0x800), ("D_800F6F3C_B177C", 0x100)):
        allocation(name, session.word(symbols[name]), size)

    tables = []
    for name, index, blob_index, pointer, counter in (
        ("textures", 3, 2, symbols["D_800FF9C0"], symbols["D_800FF9D8"]),
        ("textures_id_bit15", 1, 0, symbols["D_800FF9C0"] + 4, symbols["D_800FF9D8"] + 4),
        ("sprites", 0x16, 0x15, symbols["D_800FF9E8"], symbols["D_800FF9F4"]),
        ("models", 0x26, 0x27, symbols["D_800F6F10_B1750"], symbols["D_800F6F20_B1760"]),
    ):
        table, source = _section(rom, lut, lut_end, index)
        blob, _ = _section(rom, lut, lut_end, blob_index)
        require(len(table) >= 12 and len(table) % 4 == 0, f"Invalid {name} directory size")
        words = struct.unpack(f">{len(table) // 4}I", table)
        require(0xFFFFFFFF in words, f"Missing {name} end sentinel")
        sentinel = words.index(0xFFFFFFFF)
        offsets = words[:sentinel]
        require(len(offsets) >= 2 and offsets[0] == 0 and offsets[-1] == len(blob),
                f"{name} directory does not span its data section")
        require(all(a < b for a, b in zip(offsets, offsets[1:]))
                and all(offset % 16 == 0 for offset in offsets), f"Invalid {name} offsets")
        require(not any(words[sentinel + 1:]), f"Unexpected {name} directory padding")
        # N + 1 offsets delimit N assets. The -1 sentinel follows the final
        # endpoint and is not itself an asset or a usable data offset.
        count = len(offsets) - 1
        require(session.word(counter) == count, f"Original {name} count differs from the directory")
        address = session.word(pointer)
        allocation(name, address, len(table))
        require(session.read(address, len(table)) == table, f"{name} RAM directory differs from ROM")
        tables.append({"name": name, "table_section": index, "data_section": blob_index,
                       "entries": count, "table_bytes": len(table), "data_bytes": len(blob),
                       "rom_offset": f"0x{source:08X}", "table_sha256": hashlib.sha256(table).hexdigest()})

    for name in ("D_800FF9D0", "D_800FF9E0", "D_800FF9F8", "D_800FF9E4",
                 "D_800F6F1C_B175C", "D_800F6F24_B1764", "D_800F6F40_B1780"):
        require(session.word(symbols[name]) == 0, f"Texture/sprite/model cache {name} is not empty")
    workspace = session.word(symbols["D_800F6F28_B1768"])
    for name, offset in (("D_800F6F2C_B176C", 0x80), ("D_800F6F38_B1778", 0x80),
                         ("D_800F6F34_B1774", 0x90)):
        require(session.word(symbols[name]) == workspace + offset, f"Model cursor {name} differs")
    return {"previous_bootstrap": previous, "directories": tables,
            "allocations": [{**item, "address": f"0x{item['address']:08X}"} for item in ranges],
            "individual_textures_loaded": 0, "individual_sprites_loaded": 0,
            "individual_models_loaded": 0, "model_workspace_offsets": [0x80, 0x80, 0x90],
            "scope": "Directory and cache initialization only; no image/model decoding or rendering"}
