#!/usr/bin/env python3
"""Convert private JFG ROM assets directly to PC meshes/textures (no emulation)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
ROM_SHA1 = "493ced9008dbe932d6e91179b68e8630cf23a023"


def require(ok, why):
    if not ok:
        raise ValueError(why)


def word(data, at):
    require(0 <= at <= len(data) - 4, "Word outside asset")
    return struct.unpack_from(">I", data, at)[0]


def region(data, at, count):
    require(0 <= at <= len(data) and 0 <= count <= len(data) - at, "Range outside asset")
    return data[at:at + count]


class Assets:
    def __init__(self, rom):
        require(hashlib.sha1(rom).hexdigest() == ROM_SHA1, "Wrong US ROM")
        self.rom = rom
        self.lut = region(rom, 0xB1750, 0x130)

    def section(self, index):
        start, end = word(self.lut, (index + 1) * 4), word(self.lut, (index + 2) * 4)
        require(start <= end, "Reversed section bounds")
        return region(self.rom, 0xB1880 + start, end - start)

    def asset(self, table, data, index):
        offsets = self.section(table)
        start, end = word(offsets, index * 4), word(offsets, index * 4 + 4)
        require(start < end and not ((start | end) & 15), "Invalid asset offsets")
        return region(self.section(data), start, end - start)

    @staticmethod
    def unpack(data, at):
        require(at + 5 < len(data) and data[at + 4] == 9, "Unknown RZIP header")
        length = struct.unpack_from("<I", data, at)[0]
        require(0 < length <= 1024 * 1024, "Unbounded asset expansion")
        stream = zlib.decompressobj(-15)
        result = stream.decompress(data[at + 5:], length + 1)
        require(stream.eof and len(result) == length and not stream.unconsumed_tail, "RZIP length mismatch")
        return result

    def model(self, number):
        return self.unpack(self.asset(0x26, 0x27, number), 0)

    def texture(self, number):
        high = bool(number & 0x8000)
        data = self.asset(1 if high else 3, 0 if high else 2, number & 0x7FFF)
        raw = self.unpack(data, 32) if data[25] else data
        width, height, packed_format = raw[:3]
        fmt = packed_format & 15
        require(width > 0 and height > 0 and fmt in (0, 1, 5), "Unsupported native texture format")
        pixel_bytes = {0: 4, 1: 2, 5: 1}[fmt]
        pixels = region(raw, 32, width * height * pixel_bytes)
        decoded = bytearray()
        for y in range(height):
            for x in range(width):
                # Offline conversion of the ROM's interleaved rows. The GPU
                # receives ordinary RGBA8; no TMEM or RDP command is executed.
                lane = (x * pixel_bytes) ^ ((8 if fmt == 0 else 4) if y & 1 else 0)
                require(lane + pixel_bytes <= width * pixel_bytes, "Unsupported row layout")
                at = y * width * pixel_bytes + lane
                if fmt == 1:
                    value = int.from_bytes(pixels[at:at + 2], "big")
                    color = [((value >> shift) & 31) * 255 // 31 for shift in (11, 6, 1)] + [255 if value & 1 else 0]
                elif fmt == 5:
                    value = pixels[at]
                    color = [((value >> 4) & 15) * 17] * 3 + [(value & 15) * 17]
                else:
                    color = pixels[at:at + 4]
                decoded.extend(color)
        return {"id": number, "width": width, "height": height, "format": fmt,
                "rgba": bytes(decoded), "source_sha256": hashlib.sha256(raw).hexdigest(),
                "frame": 0}


def skeleton_nodes(model):
    count, pointer = model[0x4F], word(model, 0x54)
    require(count == 21, "Expected Juno skeleton")
    nodes = []
    for i in range(count):
        at = pointer + i * 16
        parent, slot, rotation0, rotation1 = struct.unpack(">bBBB", region(model, at, 4))
        require(-1 <= parent < i and (parent == -1) == (i == 0) and (slot, rotation0, rotation1) == (i,) * 3,
                "Unsupported skeleton hierarchy")
        local = struct.unpack(">3f", region(model, at + 4, 12))
        nodes.append((parent, local))
    return nodes


def skeleton(model):
    positions = []
    f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
    for parent, local in skeleton_nodes(model):
        positions.append(tuple(f32(x + (positions[parent][axis] if parent >= 0 else 0)) for axis, x in enumerate(local)))
    return positions


def convert(assets, animated=False, transitions=False, character=False, juno_selection=False):
    transitions = transitions or character or juno_selection
    animated = animated or transitions
    body, hand = assets.model(220), assets.model(309)
    require(body[:4] == b"Boy\0" and hand[:9] == b"JunoHand\0", "Wrong model names")
    bones = skeleton(body)
    attachment = struct.unpack(">H", region(body, word(body, 0x30) + 2, 2))[0]
    require(attachment == 6, "Original hand attachment differs")
    textures, texture_map, vertices, draws, reports = [], {}, [], [], []
    for number, model, rigid in ((220, body, False), (309, hand, True)):
        vc, tc, bc = struct.unpack(">HHH", region(model, 0x12, 6))
        require((vc, tc, bc) == ((43, 33, 4) if rigid else (660, 520, 82)), "Model counts differ")
        texture_table, vertex_table, triangle_table, batch_table = (word(model, o) for o in (0x18, 0x1C, 0x20, 0x24))
        region(model, vertex_table, vc * 10)
        region(model, triangle_table, tc * 16)
        submitted, omitted = 0, 0
        for batch in range(bc):
            record = region(model, batch_table + batch * 16, 16)
            next_record = region(model, batch_table + (batch + 1) * 16, 16)
            first_vertex, first_triangle = struct.unpack_from(">HH", record, 6)
            end_vertex, end_triangle = struct.unpack_from(">HH", next_record, 6)
            flags = word(record, 12)
            require(first_vertex <= end_vertex <= vc and first_triangle <= end_triangle <= tc, "Invalid batch ranges")
            if flags & 0x400:
                omitted += end_triangle - first_triangle
                continue
            require(record[0] < model[0x10], "Unsupported untextured visible batch")
            texture_id = struct.unpack(">H", region(model, texture_table + record[0] * 8 + 6, 2))[0]
            if texture_id not in texture_map:
                texture_map[texture_id] = len(textures)
                textures.append(assets.texture(texture_id))
            texture_index = texture_map[texture_id]
            texture = textures[texture_index]
            for triangle in range(first_triangle, end_triangle):
                tri = region(model, triangle_table + triangle * 16, 16)
                require(tri[0] in (0, 0x40), "Unsupported face flags")
                draw_flags = (1 if tri[0] & 0x40 else 0) | (2 if (flags & 4 or texture["format"] in (0, 5)) else 0)
                first = len(vertices)
                for corner, index in enumerate(tri[1:4]):
                    require(index < end_vertex - first_vertex, "Triangle index outside its batch")
                    if rigid:
                        bone = attachment
                    else:
                        bone = record[1 if index < record[4] else 2 if index < record[5] else 3]
                    require(bone < len(bones), "Invalid bone selector")
                    v = region(model, vertex_table + (first_vertex + index) * 10, 10)
                    xyz = struct.unpack_from(">3h", v)
                    uv = struct.unpack_from(">2h", tri, 4 + corner * 4)
                    # The original vertex's final byte is not ordinary PC
                    # material opacity. This initial native material is opaque
                    # vertex tint with texture alpha for cutout/translucency.
                    position = xyz if animated else [x + bones[bone][axis] for axis, x in enumerate(xyz)]
                    vertices.append((*position,
                                     (uv[0] / 32 + 0.5) / texture["width"],
                                     (uv[1] / 32 + 0.5) / texture["height"],
                                     *[x / 255 for x in v[6:9]], 1.0, *([bone] if animated else [])))
                if draws and draws[-1][0] + draws[-1][1] == first and draws[-1][2:] == [texture_index, draw_flags, number]:
                    draws[-1][1] += 3
                else:
                    draws.append([first, 3, texture_index, draw_flags, number])
                submitted += 1
        require(submitted == (32 if rigid else 502), "Visible triangle total changed")
        reports.append({"id": number, "vertices_stored": vc, "triangles_stored": tc,
                        "triangles_submitted": submitted, "triangles_omitted_by_original_flag": omitted,
                        "decoded_sha256": hashlib.sha256(model).hexdigest()})
    magic = b"JFGNAT3\0" if transitions else b"JFGNAT2\0" if animated else b"JFGNAT1\0"
    data = bytearray(struct.pack("<8sIIII", magic,
                                 len(textures), len(draws), len(vertices), len(bones) if animated else 0))
    for texture in textures:
        data.extend(struct.pack("<IIII", texture["id"], texture["width"], texture["height"], len(texture["rgba"])))
        data.extend(texture["rgba"])
    for draw in draws:
        data.extend(struct.pack("<5I", *draw))
    for vertex in vertices:
        data.extend(struct.pack("<9fI" if animated else "<9f", *vertex))
    clip_report = None
    clip_reports = []
    if animated:
        from animation_assets import clip_for_model
        for parent, local in skeleton_nodes(body):
            data.extend(struct.pack("<i3f", parent, *local))
        indices = (0, 14, 51, 1, 2, 3, 16, 28, 36) if juno_selection else (0, 14, 51) if character else (0, 14) if transitions else (0,)
        if transitions:
            data.extend(struct.pack("<I", len(indices)))
        for index in indices:
            frames, details = clip_for_model(assets, 220, index)
            clip_reports.append(details)
            data.extend(struct.pack("<IIIf", details["id"], len(frames), int(details["loop"]), 15.0))
            for root, angles in frames:
                data.extend(struct.pack("<3f", *root))
                for rotation in angles:
                    data.extend(struct.pack("<3f", *rotation))
        clip_report = clip_reports[0]
    return bytes(data), {"status": "prepared", "models": reports, "triangles": len(vertices) // 3,
                         "draws": len(draws), "texture_count": len(textures), "bones": len(bones),
                         "hand_bone": attachment, "textures": [{k: v for k, v in t.items() if k != "rgba"} for t in textures],
                         "scene_version": 3 if transitions else 2 if animated else 1, "animation": clip_report, "animations": clip_reports,
                         "character_profile": character,
                         "juno_selection_profile": juno_selection,
                         "limits": ["Selected original skeletal clips or neutral pose; ordinary PC materials, texture animation frame zero",
                                    "Offline file-format conversion; no MIPS, RSP, RDP, PIF or CIC execution",
                                    "No pixel-equivalence claim with Nintendo 64 hardware"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-native")
    parser.add_argument("--animation", action="store_true")
    parser.add_argument("--transitions", action="store_true")
    parser.add_argument("--character", action="store_true")
    parser.add_argument("--juno-selection", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    require(out.is_relative_to(ROOT / "build"), "Private assets must remain inside ignored build/")
    assets = Assets(args.rom.read_bytes())
    data, report = convert(assets, animated=args.animation, transitions=args.transitions, character=args.character, juno_selection=args.juno_selection)
    out.mkdir(parents=True, exist_ok=True)
    if args.juno_selection:
        from juno_selection_assets import prepare_selection
        selection, audit = prepare_selection(assets, args.rom)
        (out / "juno-selection.bin").write_bytes(selection)
        (out / "selection-assets-report.json").write_text(json.dumps(audit, indent=2) + "\n")
    (out / "scene.bin").write_bytes(data)
    report["scene_sha256"] = hashlib.sha256(data).hexdigest()
    (out / "assets-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "triangles", "draws", "texture_count", "bones", "scene_sha256")}))


if __name__ == "__main__":
    main()
