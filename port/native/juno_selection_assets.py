"""Read Juno selection tables and audit original code/relocations statically."""
import hashlib
import re
import struct
import sys
from prepare_assets import ROOT, region, require


def prepare_selection(assets, rom_path):
    # This repository tool parses ROM relocation records; it executes no CPU.
    sys.path.insert(0, str(ROOT / "tools"))
    from overlay_reloc import OverlayRelocTool
    reloc = OverlayRelocTool(str(rom_path))
    require(reloc.rom == assets.rom, "ROM changed between asset conversion and relocation audit")
    header = reloc.get_overlay_header(16)
    base = reloc.offsets["overlay_data_base"] + header.rom_offset
    require(header.text_size == 0x75F0 and header.data_size == 0xAE0, "Juno overlay layout changed")
    secondary = {entry.target_offset: entry for entry in reloc.get_relocation_entries(16, secondary=True)}
    for high, low in ((0x4FAC, 0x4FB0), (0x5004, 0x5008), (0x5050, 0x5054), (0x5088, 0x508C)):
        for location, kind in ((high, 5), (low, 6)):
            entry = secondary[location]
            require((entry.reloc_type, entry.patch_type, entry.symbol_index) == (1, kind, header.text_size),
                    "Remapping table relocation differs")
        require(struct.unpack_from(">I", assets.rom, base + low)[0] & 0xFFFF == 0x280, "Remapping table addend differs")
    for location, kind in ((0x4E18, 5), (0x4E60, 6)):
        entry = secondary[location]
        require((entry.reloc_type, entry.patch_type, entry.symbol_index) == (1, kind, 0x7E20), "Threshold relocation differs")
    require(struct.unpack_from(">I", assets.rom, base + 0x4E60)[0] & 0xFFFF == 0x144, "Threshold addend differs")
    primary = {entry.target_offset: entry for entry in reloc.get_relocation_entries(16)}
    calls = {0x4ED0: "mathRnd", 0x4FEC: "controlPlayerGunWeight", 0x5038: "controlPlayerGunWeight",
             0x50D4: "objAnimSetMove", 0x5104: "controlSetTransition"}
    for location, name in calls.items():
        entry = primary[location]
        require((entry.reloc_type, entry.patch_type) == (0, 4) and
                reloc.resolve_reloc_target(entry, header, 16)[1] == name, "Original selection dependency differs")

    code_checks = []
    for name, source, size in (
        ("juno_choose_move", "overlays/o16/overlay_16/func_overlay_16_01004E08_1F22E20.s", 0x170),
        ("juno_remap_move", "overlays/o16/overlay_16/func_overlay_16_01004F78_1F22F90.s", 0x1A8),
        ("controlPlayerGunWeight", "charControl/controlPlayerGunWeight.s", 0x58),
        ("objAnimSetMove", "objects/objAnimSetMove.s", 0x220),
        ("controlSetTransition", "charControl/controlSetTransition.s", 0x114),
    ):
        text = (ROOT / "asm/nonmatchings" / source).read_text()
        words = [(int(a, 16), int(w, 16)) for a, w in re.findall(r"/\*\s+([0-9A-F]{1,8})\s+[0-9A-F]{8}\s+([0-9A-F]{8})\s+\*/", text)]
        require(len(words) * 4 == size, "Unexpected original routine extent")
        require(all(address == words[0][0] + i * 4 for i, (address, _) in enumerate(words)), "Noncontiguous original routine")
        data = b"".join(struct.pack(">I", word) for _, word in words)
        require(region(assets.rom, words[0][0], size) == data, "ASM listing no longer matches the ROM")
        code_checks.append({"name": name, "source": "asm/nonmatchings/" + source, "bytes_checked": size,
                            "rom_offset": hex(words[0][0]), "sha256": hashlib.sha256(data).hexdigest()})

    first, end = struct.unpack(">HH", region(assets.section(0x28), 220 * 2, 4))
    ids = struct.unpack(">52H", region(assets.section(0x29), first, end - first))
    rows = region(assets.rom, base + header.text_size + 0x280, len(ids) * 5)
    threshold = struct.unpack(">f", region(assets.rom, base + 0x7E20 + 0x144, 4))[0]
    require(threshold == struct.unpack("<f", struct.pack("<f", 0.1))[0], "Original stationary threshold changed")
    require(len(set(ids)) == len(ids), "Duplicate source clip IDs")
    payload = bytearray(struct.pack("<8sIf", b"JFGSEL1\0", len(ids), threshold))
    for index, clip_id in enumerate(ids):
        row = rows[index * 5:index * 5 + 5]
        require(all(value < len(ids) for value in row[:3]) and all(value < 7 for value in row[3:]), "Invalid original selection row")
        payload.extend(struct.pack("<5BxH", *row, clip_id))
    return bytes(payload), {"status": "prepared", "row_count": len(ids), "transition_profiles": 7,
                           "stationary_threshold": threshold, "table_rom_offset": hex(base + header.text_size + 0x280),
                           "table_sha256": hashlib.sha256(rows).hexdigest(), "routine_audits": code_checks,
                           "call_relocations_checked": len(calls), "data_relocations_checked": 10,
                           "payload_sha256": hashlib.sha256(payload).hexdigest(), "emulation_used": False,
                           "scope": "Static original selection decisions; transition profile contents and whole gameplay loop are not ported"}
