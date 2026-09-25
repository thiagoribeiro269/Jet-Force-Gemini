#!/usr/bin/env python3
"""Convert a bounded original US region directly to native mesh/collision data."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
from prepare_assets import Assets, ROOT, region, require, word


def indexed(assets, table, data, index):
    directory = assets.section(table)
    values = [value[0] for value in struct.iter_unpack(">i", directory)]
    require(-1 in values, "Missing original directory sentinel")
    offsets = values[:values.index(-1)]
    require(len(offsets) >= 2 and 0 <= index < len(offsets) - 1, "Original asset index outside directory")
    require(all(a <= b for a, b in zip(offsets, offsets[1:])), "Unordered original directory")
    require(offsets[-1] <= len(assets.section(data)), "Original directory exceeds data section")
    return region(assets.section(data), offsets[index], offsets[index + 1] - offsets[index])


def object_definition(assets, object_id):
    index = struct.unpack(">H", region(assets.section(0x30), object_id * 2, 2))[0]
    raw = indexed(assets, 0x2E, 0x2F, index)
    require(len(raw) >= 0xE0, "Truncated original object definition")
    return raw, index


def audit_sources(assets):
    sources = ["level/levelInit", "level/levelGetName", "objects/objLoadObjList", "objects/objGetControlNo",
               "objects/objSetupPlayers", "objects/objSetupObject", "track/func_80014B6C",
               "overlays/o24/overlay_24/trackInit", "overlays/o24/overlay_24/func_overlay_24_018001F8_1F41D28"]
    reports = []
    for source in sources:
        path = ROOT / "asm/nonmatchings" / (source + ".s")
        text = path.read_text(); name = source.rsplit("/", 1)[-1]
        start, end = text.index("glabel " + name), text.index("endlabel " + name)
        words = [(int(a, 16), int(w, 16)) for a, w in re.findall(r"/\*\s+([0-9A-F]{1,8})\s+[0-9A-F]{8}\s+([0-9A-F]{8})\s+\*/", text[start:end])]
        size = int(re.search(r"nonmatching " + re.escape(name) + r", (0x[0-9A-Fa-f]+)", text).group(1), 16)
        require(len(words) * 4 == size and all(a == words[0][0] + i * 4 for i, (a, _) in enumerate(words)), "Unbounded original routine")
        data = b"".join(struct.pack(">I", word) for _, word in words)
        require(region(assets.rom, words[0][0], size) == data, "Source listing differs from original ROM")
        reports.append({"source": str(path.relative_to(ROOT)), "bytes": size, "sha256": hashlib.sha256(data).hexdigest()})
    return reports


def convert_region(assets, level_id=21):
    require(level_id == 21, "Only the inspected Forest First profile is enabled")
    header = indexed(assets, 0x1E, 0x1F, level_id)
    require(len(header) == 0x118, "Unexpected US level header")
    name = header[:32].split(b"\0")[0].decode("ascii")
    geometry, objects, sky = struct.unpack_from(">3h", header, 0x54)
    secondary_objects = struct.unpack_from(">h", header, 0xCA)[0]
    require((name, geometry, objects, secondary_objects, sky) == ("Forest First", 17, 424, 17, 180), "Region binding differs")
    raw = assets.unpack(indexed(assets, 0x24, 0x25, geometry), 0)
    require(len(raw) == 99206, "Geometry profile changed")
    texture_table, block_table, box_table = (word(raw, offset) for offset in (0, 4, 8))
    texture_slots, block_count = struct.unpack_from(">2H", raw, 0x18)
    require((texture_slots, block_count) == (45, 13), "Unexpected region directory size")
    extents = struct.unpack_from(">6h", raw, 0x20)
    bounds_min = (extents[0], extents[2], extents[4]); bounds_max = (extents[1], extents[3], extents[5])
    region(raw, texture_table, texture_slots * 8); region(raw, block_table, block_count * 0x48); region(raw, box_table, block_count * 12)
    textures, texture_map, vertices, draws, collision = [], {}, [], [], []
    stored_vertices = stored_triangles = stored_batches = omitted = 0
    block_reports = []; source_flags = Counter()
    for block in range(block_count):
        at = block_table + block * 0x48
        vertex_table, triangle_table, batches = word(raw, at), word(raw, at + 4), word(raw, at + 12)
        vertex_count, triangle_count, batch_count = struct.unpack_from(">3H", raw, at + 0x24)
        require(vertex_count > 0 and triangle_count > 0 and batch_count > 0, "Empty geometry block")
        region(raw, vertex_table, vertex_count * 10); region(raw, triangle_table, triangle_count * 16); region(raw, batches, (batch_count + 1) * 16)
        box = struct.unpack_from(">6h", raw, box_table + block * 12)
        points = [struct.unpack_from(">3h", raw, vertex_table + i * 10) for i in range(vertex_count)]
        require(all(box[a] <= p[a] <= box[a + 3] for p in points for a in range(3)), "Vertex outside original block box")
        stored_vertices += vertex_count; stored_triangles += triangle_count; stored_batches += batch_count
        covered = 0; visible = 0
        for batch in range(batch_count):
            record = region(raw, batches + batch * 16, 16)
            next_record = region(raw, batches + (batch + 1) * 16, 16)
            first_vertex, first_triangle = struct.unpack_from(">2H", record, 6)
            end_vertex, end_triangle = struct.unpack_from(">2H", next_record, 6)
            flags = word(record, 12); source_flags[hex(flags)] += 1
            require(first_triangle == covered and first_triangle <= end_triangle <= triangle_count and
                    first_vertex <= end_vertex <= vertex_count, "Noncontiguous or invalid region batch")
            covered = end_triangle
            hidden = bool(flags & 0x400)  # Filtered in every draw pass of func_80014B6C.
            if not hidden:
                require(record[0] < texture_slots, "Untextured region batch requires a converter extension")
                texture_id = word(raw, texture_table + record[0] * 8) | 0x8000
                if texture_id not in texture_map:
                    texture_map[texture_id] = len(textures); textures.append(assets.texture(texture_id))
                texture_index = texture_map[texture_id]; texture = textures[texture_index]
            for triangle in range(first_triangle, end_triangle):
                face = region(raw, triangle_table + triangle * 16, 16)
                require(face[0] in (0, 0x40), "Uninspected region face flags")
                require(all(index < end_vertex - first_vertex for index in face[1:4]), "Region triangle exceeds batch vertices")
                positions = [points[first_vertex + index] for index in face[1:4]]
                collision.append((*[v for position in positions for v in position], face[0]))
                if hidden:
                    omitted += 1; continue
                # Native material policy: repeated terrain UVs, vertex tint and
                # texture alpha. Other source material effects remain explicit limits.
                draw_flags = 12 | (1 if face[0] & 0x40 else 0) | (2 if texture["format"] in (0, 5) or flags & 4 else 0)
                first = len(vertices)
                for corner, index in enumerate(face[1:4]):
                    vertex = region(raw, vertex_table + (first_vertex + index) * 10, 10)
                    u, v = struct.unpack_from(">2h", face, 4 + corner * 4)
                    vertices.append((*positions[corner], (u / 32 + 0.5) / texture["width"], (v / 32 + 0.5) / texture["height"],
                                     *[c / 255 for c in vertex[6:9]], 1.0))
                tag = 0x10000 | block
                if draws and draws[-1][0] + draws[-1][1] == first and draws[-1][2:] == [texture_index, draw_flags, tag]:
                    draws[-1][1] += 3
                else:
                    draws.append([first, 3, texture_index, draw_flags, tag])
                visible += 1
        require(covered == triangle_count, "Region triangle coverage incomplete")
        block_reports.append({"block": block, "vertices": vertex_count, "stored_triangles": triangle_count, "visible_triangles": visible, "batches": batch_count})
    require((stored_vertices, stored_triangles, stored_batches) == (3539, 2342, 352), "Original region totals changed")
    require(omitted == 324 and len(vertices) // 3 == 2018, "Visible region profile changed")

    # Select the first original setup-point record. The selected record's
    # player/group/angle fields are all zero; no general entry-selection policy is inferred.
    object_list = indexed(assets, 0x1C, 0x1D, objects)
    object_bytes = word(object_list, 0); region(object_list, 16, object_bytes)
    at = 16; setups = []; object_count = 0
    while at < 16 + object_bytes:
        record_size = object_list[at + 2]
        require(record_size >= 10 and at + record_size <= 16 + object_bytes, "Invalid object record size")
        record = region(object_list, at, record_size); object_id = struct.unpack_from(">H", record)[0]
        definition, definition_index = object_definition(assets, object_id)
        if struct.unpack_from(">h", definition, 0x1C)[0] == 6:
            setups.append({"offset": at, "object_id": object_id, "definition": definition_index, "record": record})
        at += record_size; object_count += 1
    require(at == 16 + object_bytes and setups, "Object list boundary or setup points differ")
    entry = setups[0]; record = entry["record"]
    require((entry["offset"], entry["object_id"], entry["definition"], len(record)) == (16, 13, 95, 14) and record[10:] == bytes(4),
            "First inspected setup point changed")
    spawn = struct.unpack_from(">3h", record, 4)
    require(spawn == (40, 19, 841), "Source spawn position changed")
    boy, boy_definition = object_definition(assets, 126)
    require(boy_definition == 1 and boy[4:20].split(b"\0")[0] == b"playerBoy" and word(boy, word(boy, 0x30)) == 220,
            "Juno object/model binding differs")
    scale = struct.unpack_from(">f", boy)[0]
    require(abs(scale - 0.26) < 1e-7, "Original Juno scale differs")

    mesh = bytearray(struct.pack("<8sIIII", b"JFGWRL1\0", len(textures), len(draws), len(vertices), 0))
    for texture in textures:
        mesh.extend(struct.pack("<4I", texture["id"], texture["width"], texture["height"], len(texture["rgba"])))
        mesh.extend(texture["rgba"])
    for draw in draws: mesh.extend(struct.pack("<5I", *draw))
    for vertex in vertices: mesh.extend(struct.pack("<9f", *vertex))
    info = bytearray(struct.pack("<8s6I11f32s", b"JFGREG1\0", level_id, geometry, block_count, len(vertices) // 3, len(collision), 126,
                                *spawn, 0.0, scale, *bounds_min, *bounds_max, name.encode()))
    for triangle in collision: info.extend(struct.pack("<9fI", *triangle))
    report = {"status": "prepared", "level": level_id, "name": name, "geometry": geometry, "source_geometry_bytes": len(raw),
              "source_geometry_sha256": hashlib.sha256(raw).hexdigest(), "source_header_sha256": hashlib.sha256(header).hexdigest(),
              "object_lists": [objects, secondary_objects], "object_records_inspected": object_count, "setup_points": len(setups),
              "spawn_object": entry["object_id"], "spawn_definition": entry["definition"], "spawn_record_offset": entry["offset"],
              "source_spawn": list(spawn), "player_object": 126, "player_definition": boy_definition, "player_model": 220, "player_scale": scale,
              "blocks": block_reports, "stored_vertices": stored_vertices, "stored_triangles": stored_triangles,
              "visible_triangles": len(vertices) // 3, "hidden_triangles": omitted, "collision_triangles": len(collision),
              "draws": len(draws), "source_texture_slots": texture_slots, "textures_converted": len(textures),
              "texture_formats": dict(Counter(t["format"] for t in textures)), "source_batch_flags": dict(source_flags),
              "bounds_min": bounds_min, "bounds_max": bounds_max,
              "mesh_sha256": hashlib.sha256(mesh).hexdigest(), "region_info_sha256": hashlib.sha256(info).hexdigest(),
              "source_audits": audit_sources(assets), "emulation_used": False,
              "limits": ["Static geometry and native material policy; original sky, weather, scrolling and special effects not integrated",
                         "Object records inspected for the entry point; their behaviors are not instantiated",
                         "Ground query data includes hidden geometry; this does not implement the original full collision engine"]}
    return bytes(mesh), bytes(info), report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-native/region")
    args = parser.parse_args(); out = args.out.resolve()
    require(out.is_relative_to(ROOT / "build"), "Region assets must remain under ignored build/")
    mesh, info, report = convert_region(Assets(args.rom.read_bytes()))
    out.mkdir(parents=True, exist_ok=True)
    (out / "region-mesh.bin").write_bytes(mesh); (out / "region-info.bin").write_bytes(info)
    (out / "region-assets-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "level", "name", "visible_triangles", "textures_converted", "source_spawn", "player_scale")}))


if __name__ == "__main__":
    main()
