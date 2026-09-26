#!/usr/bin/env python3
"""Convert Forest First collision and Juno movement data to private PC resources.

Static conversion only. The routines named below were read from their ASM
listings and their bytes are audited against the local US ROM; nothing here
executes MIPS code, applies relocations to live memory or emulates hardware.
Load-time derivations (collision masks, planes and exposed edges) are left to
the C++ port so the PC code carries the recovered original algorithms.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

from prepare_assets import ROOT, region, require, word
from region_assets import indexed, object_definition

sys.path.insert(0, str(ROOT / "tools"))
from overlay_reloc import OverlayRelocTool  # noqa: E402

MAIN_TO_ROM = 0x7FFFF400  # Main segment: ROM 0x1000 is RAM 0x80000400.

# Listings whose instructions were read to recover the collision and movement
# contracts. Each one is compared word by word with the ROM before conversion.
SOURCE_LISTINGS = [
    ("asm/hasm/trackasm.s", "trackMakePolylist"), ("asm/hasm/trackasm.s", "getXZCompareMask"),
    ("asm/hasm/trackasm.s", "getYCompareMask"), ("asm/nonmatchings/track/trackGetBlockList.s", "trackGetBlockList"),
    ("asm/nonmatchings/track/trackPolyHeight.s", "trackPolyHeight"),
    ("asm/nonmatchings/track/trackGetPlayerIntersect.s", "trackGetPlayerIntersect"),
    ("asm/nonmatchings/track/func_80016EA0.s", "func_80016EA0"), ("asm/nonmatchings/track/func_800175A0.s", "func_800175A0"),
    ("asm/nonmatchings/track/func_800182C0.s", "func_800182C0"),
    ("asm/nonmatchings/track/trackCylinderIntersect.s", "trackCylinderIntersect"),
    ("asm/nonmatchings/track/trackSphereIntersect.s", "trackSphereIntersect"),
    ("asm/nonmatchings/objects/objMoveXYZ.s", "objMoveXYZ"), ("asm/nonmatchings/objects/objObjectsTick.s", "objObjectsTick"),
    ("asm/nonmatchings/charControl/controlGroundHits.s", "controlGroundHits"),
    ("asm/nonmatchings/charControl/func_80035628.s", "func_80035628"),
    ("asm/nonmatchings/charControl/func_800344C8.s", "func_800344C8"),
    ("asm/nonmatchings/charControl/controlPlatform.s", "controlPlatform"),
    ("asm/nonmatchings/charControl/controlPlayerInit.s", "controlPlayerInit"),
    ("asm/nonmatchings/charControl/controlHalfTurn.s", "controlHalfTurn"),
    ("asm/nonmatchings/charControl/controlWalkingBack.s", "controlWalkingBack"),
    ("asm/nonmatchings/charControl/dAngle.s", "dAngle"),
    ("asm/nonmatchings/overlays/o24/overlay_24/func_overlay_24_01800908_1F42438.s", "func_overlay_24_01800908_1F42438"),
    ("asm/nonmatchings/overlays/o24/overlay_24/func_overlay_24_01800CE4_1F42814.s", "func_overlay_24_01800CE4_1F42814"),
    ("asm/nonmatchings/overlays/o24/overlay_24/func_overlay_24_018001F8_1F41D28.s", "func_overlay_24_018001F8_1F41D28"),
    ("asm/nonmatchings/overlays/o16/overlay_16/boyControl.s", "boyControl"),
    ("asm/nonmatchings/overlays/o16/overlay_16/func_overlay_16_01002708_1F20720.s", "func_overlay_16_01002708_1F20720"),
    ("asm/nonmatchings/overlays/o16/overlay_16/func_overlay_16_01003464_1F2147C.s", "func_overlay_16_01003464_1F2147C"),
    ("asm/nonmatchings/overlays/o16/overlay_16/func_overlay_16_01004934_1F2294C.s", "func_overlay_16_01004934_1F2294C"),
    ("asm/nonmatchings/overlays/o16/overlay_16/func_overlay_16_01005BB8_1F23BD0.s", "func_overlay_16_01005BB8_1F23BD0"),
    ("asm/nonmatchings/overlays/o16/overlay_16/func_overlay_16_01005120_1F23138.s", "func_overlay_16_01005120_1F23138"),
    ("asm/nonmatchings/objects/objAnimDframe.s", "objAnimDframe"), ("asm/nonmatchings/objects/objAnimSetMove.s", "objAnimSetMove"),
]

# Overlay 16 data constants used by the ported walking/air/movement code, in
# the order read by juno_body.h. Offsets are relative to the overlay data base.
OVERLAY16_CONSTANTS = [
    ("turnAimRate", 0x8B0), ("turnAimTarget", 0x8B4), ("turnRate", 0x8B8), ("turnTarget", 0x8BC),
    ("turnFloor", 0x8C0), ("halfTurnFactor", 0x8C4), ("speedRate", 0x8C8), ("brakeTrigger", 0x8CC),
    ("leanFactor", 0x8D0), ("lateralDecay", 0x968), ("lateralZeroLow", 0x96C), ("lateralZeroHigh", 0x970),
    ("slopeBias", 0xA7C), ("slopeLimit", 0xA80), ("lockedDamping", 0xA84), ("jumpAnimFloor", 0x8E8),
    ("airSpeedScale", 0x908), ("airSpeedBase", 0x90C), ("airTurnScale", 0x910), ("airTurnBase", 0x914),
]


def audit_listing(assets, path, name):
    text = (ROOT / path).read_text()
    start = text.index("glabel " + name)
    end = text.index("endlabel " + name, start)
    words = [(int(a, 16), int(w, 16)) for a, w in
             re.findall(r"/\*\s+([0-9A-F]{1,8})\s+[0-9A-F]{8}\s+([0-9A-F]{8})\s+\*/", text[start:end])]
    size = int(re.search(r"nonmatching " + re.escape(name) + r", (0x[0-9A-Fa-f]+)", text).group(1), 16)
    require(words and len(words) * 4 == size, f"Unbounded original routine {name}")
    require(all(a == words[0][0] + i * 4 for i, (a, _) in enumerate(words)), f"Noncontiguous routine {name}")
    data = b"".join(struct.pack(">I", w) for _, w in words)
    require(region(assets.rom, words[0][0], size) == data, f"Listing {name} differs from the ROM")
    return {"source": path, "routine": name, "rom_offset": hex(words[0][0]), "bytes": size,
            "sha256": hashlib.sha256(data).hexdigest()}


def main_bytes(assets, address, count):
    return region(assets.rom, address - MAIN_TO_ROM, count)


def convert_collision(assets, level_id=21):
    require(level_id == 21, "Only the inspected Forest First profile is enabled")
    header = indexed(assets, 0x1E, 0x1F, level_id)
    require(len(header) == 0x118 and header[:12] == b"Forest First", "Level header differs")
    geometry = struct.unpack_from(">h", header, 0x54)[0]
    poly_capacity, edge_capacity = struct.unpack_from(">2h", header, 0xEA)
    no_hits = header[0x6C] in (1, 2)  # trackInit: nohitsflag when level +0x6C is 1 or 2.
    require((geometry, poly_capacity, edge_capacity, no_hits) == (17, 120, 80, False), "Collision profile differs")
    raw = assets.unpack(indexed(assets, 0x24, 0x25, geometry), 0)
    require(len(raw) == 99206, "Geometry profile changed")
    texture_table, block_table, box_table = word(raw, 0), word(raw, 4), word(raw, 8)
    texture_slots, block_count = struct.unpack_from(">2H", raw, 0x18)
    extents = struct.unpack_from(">6h", raw, 0x20)
    surfaces = bytes(region(raw, texture_table + i * 8, 8)[7] for i in range(texture_slots))
    out = bytearray(struct.pack("<8s7I6h", b"JFGCOL1\0", level_id, geometry, block_count, texture_slots,
                                poly_capacity, edge_capacity, int(no_hits), *extents))
    out.extend(surfaces)
    out.extend(bytes(-len(out) % 4))
    totals = {"vertices": 0, "triangles": 0, "batches": 0, "shared_edges": 0, "boundary_edges": 0, "one_sided_links": 0}
    for block in range(block_count):
        at = block_table + block * 0x48
        vertex_table, triangle_table, batches, entries = word(raw, at), word(raw, at + 4), word(raw, at + 12), word(raw, at + 0x18)
        vertex_count, triangle_count, batch_count = struct.unpack_from(">3H", raw, at + 0x24)
        box = struct.unpack_from(">6h", raw, box_table + block * 12)
        out.extend(struct.pack("<6h3I", *box, vertex_count, triangle_count, batch_count))
        for i in range(vertex_count):
            out.extend(struct.pack("<3h", *struct.unpack_from(">3h", raw, vertex_table + i * 10)))
        out.extend(bytes(-len(out) % 4))
        faces = []
        for i in range(triangle_count):
            face = region(raw, triangle_table + i * 16, 16)
            # Face bits 0/1 select the ledge pairing path of the load routine,
            # 0x80 disables collision. Neither occurs in this inspected profile.
            require(face[0] in (0, 0x40), "Uninspected collision face flags")
            faces.append(face[:4]); out.extend(face[:4])
        covered = 0
        for i in range(batch_count + 1):
            record = region(raw, batches + i * 16, 16)
            first_vertex, first_triangle = struct.unpack_from(">2H", record, 6)
            flags = word(record, 12)
            if i < batch_count:
                require(first_triangle == covered and record[0] < texture_slots, "Invalid collision batch")
            else:
                require(first_triangle == triangle_count, "Collision batch sentinel differs")
            covered = struct.unpack_from(">H", region(raw, batches + (i + 1) * 16, 16), 8)[0] if i < batch_count else covered
            out.extend(struct.pack("<BxHHxxI", record[0], first_vertex, first_triangle, flags))
        links = [struct.unpack_from(">4H", raw, entries + i * 8) for i in range(triangle_count)]
        for i, link in enumerate(links):
            require(link[0] == i, "Face plane order differs from the inspected sequential profile")
            for neighbor in link[1:]:
                require(neighbor < triangle_count, "Neighbor outside block")
                if neighbor == i:
                    totals["boundary_edges"] += 1
                else:
                    totals["shared_edges"] += 1
                    totals["one_sided_links"] += i not in links[neighbor][1:]
            out.extend(struct.pack("<4H", *link))
        totals["vertices"] += vertex_count; totals["triangles"] += triangle_count; totals["batches"] += batch_count
    require((totals["vertices"], totals["triangles"], totals["batches"]) == (3539, 2342, 352), "Collision totals changed")
    require((totals["shared_edges"], totals["boundary_edges"], totals["one_sided_links"]) == (5778, 1248, 162),
            "Neighbor profile changed")
    report = {"status": "prepared", "level": level_id, "geometry": geometry, "blocks": block_count,
              "polylist_capacity": poly_capacity, "edge_capacity": edge_capacity, "no_hits": no_hits,
              "surface_bytes": sorted(set(surfaces)), "track_extents": extents, **totals,
              "sha256": hashlib.sha256(out).hexdigest()}
    return bytes(out), report


def convert_physics(assets, rom_path):
    audits = [audit_listing(assets, path, name) for path, name in SOURCE_LISTINGS]
    # controlPlayerInit, character types 0/4 (Juno): five spheres, exclude and
    # force-include batch masks, sphere table D_800A1BF8. The immediates are
    # checked in the audited instruction words below.
    init = (ROOT / "asm/nonmatchings/charControl/controlPlayerInit.s").read_text()
    for pattern in (r"lui\s+\$\w+, \(0xCE002000 >> 16\)", r"lui\s+\$\w+, \(0x2000000 >> 16\)",
                    r"%hi\(D_800A1BF8_A27F8\)"):
        require(re.search(pattern, init), f"Juno collision setup differs: {pattern}")
    table = main_bytes(assets, 0x800A1BF8, 8)
    spheres_at = struct.unpack_from(">I", table)[0]
    masks = table[4:8]
    require(spheres_at == 0x800A1B58 and masks == bytes((0x18, 0x01, 0x08, 0x16)), "Juno sphere table differs")
    spheres = []
    for i in range(5):
        address = struct.unpack_from(">I", main_bytes(assets, spheres_at + i * 4, 4))[0]
        raw = main_bytes(assets, address, 0x14)
        x, y, z, radius = struct.unpack_from(">4f", raw)
        spheres.append((x, y, z, radius, raw[0x10], raw[0x11]))
    require([s[:4] for s in spheres] == [(0, 13, 0, 13), (0, 26, 0, 13), (0, 39, 0, 13), (0, 50, 0, 3), (0, 30, 0, 8)]
            and [s[4:] for s in spheres] == [(2, 1), (8, 1), (8, 1), (4, 1), (8, 1)], "Juno spheres differ")
    # objGetTable(1)/(2) from asset sections 0x18/0x19 (objInitObjects, overlay 34).
    tables = assets.section(0x18); index = []
    for at in range(0, len(assets.section(0x19)), 4):
        value = struct.unpack_from(">i", assets.section(0x19), at)[0]
        if value == -1:
            break
        index.append(value)
    gravity_character = struct.unpack_from(">4f", tables, index[1] * 4)
    gravity_state = struct.unpack_from(">3f", tables, index[2] * 4)
    require(index[1:3] == [3, 7] and index[3] == 10 and gravity_character == (0.44999998807907104,) * 4
            and gravity_state == (1.0, 0.550000011920929, 1.3300000429153442), "Original gravity tables differ")
    reloc = OverlayRelocTool(str(rom_path))
    require(reloc.rom == assets.rom, "ROM changed during movement conversion")
    header = reloc.get_overlay_header(16)
    require(header.text_size == 0x75F0 and header.data_size == 0xAE0, "Juno overlay layout changed")
    data_base = reloc.offsets["overlay_data_base"] + header.rom_offset + header.text_size
    constants = [struct.unpack_from(">f", assets.rom, data_base + offset)[0] for _, offset in OVERLAY16_CONSTANTS]
    # func_overlay_16_01005120: per-move animation rates (data +0x384) and
    # the move-machine thresholds at data +0xA44..+0xA78.
    rates = struct.unpack_from(">52f", assets.rom, data_base + 0x384)
    thresholds = struct.unpack_from(">14f", assets.rom, data_base + 0xA44)
    require(rates[0] == struct.unpack("<f", struct.pack("<f", 0.015))[0] and thresholds[0] == struct.unpack("<f", struct.pack("<f", 0.1))[0],
            "Original animation rate table differs")
    # Sinf/Cosf and Arctanf tables (src/hasm/ido/math_data_tables.s).
    sine = struct.unpack_from(">1025f", main_bytes(assets, 0x800A7D94, 1025 * 4))
    arctan = struct.unpack_from(">1025h", main_bytes(assets, 0x800A8D98, 1025 * 2))
    require(sine[0] == 0 and sine[1024] == 1 and arctan[0] == 0 and arctan[1024] == 0x2000, "Math tables differ")
    out = bytearray(struct.pack("<8sII", b"JFGPHY1\0", len(spheres), len(constants)))
    for sphere in spheres:
        out.extend(struct.pack("<4fBBxx", *sphere))
    out.extend(masks)
    out.extend(struct.pack("<II", 0xCE002000, 0x02000000))
    out.extend(struct.pack("<4f3f", *gravity_character, *gravity_state))
    out.extend(struct.pack(f"<{len(constants)}f", *constants))
    rng_seed = struct.unpack(">I", main_bytes(assets, 0x800A33E4, 4))[0]  # rngSeed in main .data
    require(rng_seed == 0x5141564D, "Initial rngSeed differs")
    out.extend(struct.pack("<I", rng_seed))
    out.extend(struct.pack("<52f", *rates))
    out.extend(struct.pack("<14f", *thresholds))
    out.extend(struct.pack("<1025f", *sine))
    out.extend(struct.pack("<1025h", *arctan))
    out.extend(bytes(-len(out) % 4))
    report = {"status": "prepared", "spheres": [list(s) for s in spheres], "masks": list(masks),
              "exclude_mask": "0xCE002000", "include_mask": "0x02000000",
              "gravity_character": gravity_character, "gravity_state": gravity_state,
              "constants": {name: value for (name, _), value in zip(OVERLAY16_CONSTANTS, constants)},
              "animation_rates": rates, "move_thresholds": thresholds, "initial_rng_seed": hex(rng_seed),
              "routine_audits": audits, "emulation_used": False, "sha256": hashlib.sha256(out).hexdigest(),
              "limits": ["Juno walking/air subset; object hit models, water, ledges, weapons and other states not converted",
                         "Anti-tamper branch of boyControl (arithmeticSums) follows the legitimate-ROM path"]}
    return bytes(out), report


CAMERA_LISTINGS = [
    ("asm/nonmatchings/charControl/func_8002B378.s", "func_8002B378"), ("asm/nonmatchings/charControl/func_8002CF78.s", "func_8002CF78"),
    ("asm/nonmatchings/charControl/func_8002CBD0.s", "func_8002CBD0"), ("asm/nonmatchings/charControl/func_8002F2BC.s", "func_8002F2BC"),
    ("asm/nonmatchings/charControl/func_8002F45C.s", "func_8002F45C"), ("asm/nonmatchings/charControl/func_8002F0E8.s", "func_8002F0E8"),
    ("asm/nonmatchings/charControl/func_8002EDA0.s", "func_8002EDA0"), ("asm/nonmatchings/camera/func_8003F66C.s", "func_8003F66C"),
    ("asm/nonmatchings/camera/camSetProjMtx.s", "camSetProjMtx"), ("asm/nonmatchings/camera/camSetFOV.s", "camSetFOV"),
    ("asm/nonmatchings/camera/camInit.s", "camInit"), ("asm/nonmatchings/level/levelGetCamera.s", "levelGetCamera"),
    ("asm/nonmatchings/track/trackNearestIntersection.s", "trackNearestIntersection"),
    ("asm/nonmatchings/track/trackCylinderHeights.s", "trackCylinderHeights"), ("asm/nonmatchings/track/trackClip3D.s", "trackClip3D"),
    ("asm/nonmatchings/track/func_80019324.s", "func_80019324"), ("asm/nonmatchings/track/func_8001A990.s", "func_8001A990"),
    ("asm/nonmatchings/track/trackGetCubeBlockList.s", "trackGetCubeBlockList"),
]


def level_camera_objects(assets, header):
    """Count the level objects that replace the free camera (control numbers read like objGetControlNo)."""
    control = lambda object_id: struct.unpack_from(">h", object_definition(assets, object_id)[0], 0x1C)[0]
    kinds = {"override": (130, b"OverrideCamera"), "static": (12, b"StaticCamera"), "cutscene": (430, b"cutcamera")}
    numbers = {}
    for kind, (object_id, name) in kinds.items():
        require(object_definition(assets, object_id)[0].find(name) >= 0, "Camera object definition differs")
        numbers[control(object_id)] = kind
    require(sorted(numbers) == [17, 19, 100], "Camera object control numbers differ")
    counts = {kind: 0 for kind in kinds}
    for list_id in (struct.unpack_from(">h", header, 0x56)[0], struct.unpack_from(">h", header, 0xCA)[0]):
        objects = indexed(assets, 0x1C, 0x1D, list_id); size = word(objects, 0); at = 16
        while at < 16 + size:
            record_size = objects[at + 2]
            require(record_size >= 10 and at + record_size <= 16 + size, "Invalid object record size")
            kind = numbers.get(control(struct.unpack_from(">H", objects, at)[0]))
            if kind: counts[kind] += 1
            at += record_size
        require(at == 16 + size, "Object list boundary differs")
    return counts


def convert_camera(assets, level_id=21):
    """Free-camera tables (charControl data at 0x800A2BE0..0x800A2D38) and the level camera mode."""
    audits = [audit_listing(assets, path, name) for path, name in CAMERA_LISTINGS]
    header = indexed(assets, 0x1E, 0x1F, level_id)
    # OverrideCamera objects feed the zones read by func_8002F2BC; Forest First places none,
    # nor static cameras. Its two cutscene cameras stay outside the free camera.
    camera_objects = level_camera_objects(assets, header)
    require(camera_objects == {"override": 0, "static": 0, "cutscene": 2}, "Level camera objects differ from the inspected profile")
    mode = header[0xE3]  # levelGetCamera: level header +0xE3; camera collision mode is value + 1
    require(level_id == 21 and mode == 0, "Camera collision mode differs from the inspected profile")
    characters = [struct.unpack(">4f", main_bytes(assets, 0x800A2BE0 + i * 16, 16)) for i in range(8)]
    profiles = [struct.unpack(">9f", main_bytes(assets, address, 36)) for address in
                (0x800A2C60, 0x800A2C84, 0x800A2CA8, 0x800A2CCC, 0x800A2CF0, 0x800A2D14)]
    require(profiles[0] == struct.unpack(">9f", struct.pack(">9f", 0, 40, 0, 150, 0, 0.02, 0.125, 0.065, 0.125)) and
            characters[0] == (40.0,) * 4 and struct.unpack(">i", main_bytes(assets, 0x800A2D38, 4))[0] == 0,
            "Original camera tables differ")
    out = bytearray(struct.pack("<8sIII", b"JFGCAM1\0", level_id, mode, len(profiles)))
    for row in characters:
        out.extend(struct.pack("<4f", *row))
    for row in profiles:
        out.extend(struct.pack("<9f", *row))
    report = {"status": "prepared", "level": level_id, "camera_mode": mode + 1, "character_tables": characters, "profiles": profiles,
              "level_camera_objects": camera_objects, "routine_audits": audits, "emulation_used": False,
              "sha256": hashlib.sha256(out).hexdigest(),
              "limits": ["Free camera for player type 0 in camera collision mode 1; static, spline, lobby, cutscene and zone cameras not ported"]}
    return bytes(out), report
