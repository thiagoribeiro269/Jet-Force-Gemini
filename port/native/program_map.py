#!/usr/bin/env python3
"""Inventory the matching program and its static calls; never execute MIPS."""
import argparse
import bisect
from collections import Counter, defaultdict, deque
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from overlay_reloc import OverlayRelocTool, RelocationEntry

ROOT_NAMES = ("mainThread", "mainInitGame", "mainInitRlo", "func_80044938", "func_800457F4",
              "mainChangeLevel", "func_80046070", "levelInit", "objObjectsTick", "controlPlayer",
              "boyControl", "objPrintObject", "trackPolyHeight", "mainSaveGame")
RECOVERED = {"func_overlay_16_01004E08_1F22E20": "port/native/juno_selection.h:choose",
             "func_overlay_16_01004F78_1F22F90": "port/native/juno_selection.h:resolve"}


def require(condition, why):
    if not condition:
        raise ValueError(why)


def source_index(names):
    result = {}
    for path in sorted((ROOT / "src").rglob("*.c")):
        if "overlays_kiosk" in path.parts:
            continue  # This inventory verifies the US ELF; do not overwrite its source labels.
        text = path.read_text(errors="replace")
        for asm in re.findall(r'#pragma GLOBAL_ASM\("([^"\n]+)"\)', text):
            result[Path(asm).stem] = str(path.relative_to(ROOT))
        for name in re.findall(r"^\s*(?:static\s+)?[\w*]+(?:\s+[\w*]+)*\s+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", text, re.M):
            if name in names:
                result.setdefault(name, str(path.relative_to(ROOT)))
    return result


def inventory(elf_path, rom_path):
    tool = OverlayRelocTool(str(rom_path))
    require(hashlib.sha1(tool.rom).hexdigest() == "493ced9008dbe932d6e91179b68e8630cf23a023", "Wrong US ROM")
    with elf_path.open("rb") as file:
        elf = ELFFile(file)
        require(elf.elfclass == 32 and not elf.little_endian and elf["e_machine"] == "EM_MIPS", "Wrong ELF architecture")
        sections = list(elf.iter_sections())
        symtab = elf.get_section_by_name(".symtab")
        symbols = list(symtab.iter_symbols())
        named = {s.name: s for s in symbols}
        module_sections = {0: next(i for i, s in enumerate(sections) if s.name == ".main")}
        module_sections.update({int(s.name[9:]): i for i, s in enumerate(sections) if re.fullmatch(r"\.overlay_\d+", s.name)})
        section_modules = {v: k for k, v in module_sections.items()}
        data = {i: sections[i].data() for i in section_modules}
        rom_offsets = {}
        for i in section_modules:
            section = sections[i]
            offsets = [p["p_paddr"] + section["sh_offset"] - p["p_offset"] for p in elf.iter_segments()
                       if p["p_type"] == "PT_LOAD" and p.section_in_segment(section)]
            require(len(offsets) == 1, "Ambiguous ELF-to-ROM mapping")
            rom_offsets[i] = offsets[0]

        groups = defaultdict(list)
        for symbol in symbols:
            if symbol["st_info"]["type"] == "STT_FUNC" and symbol["st_shndx"] in section_modules:
                groups[(symbol["st_shndx"], symbol["st_value"])].append(symbol)
        names = {s.name for group in groups.values() for s in group}
        origins = source_index(names)
        nodes, exact, by_name = {}, {}, {}
        bytes_checked = 0
        for (section_index, address), group in sorted(groups.items()):
            module = section_modules[section_index]
            offset = address - sections[section_index]["sh_addr"]
            size = max(s["st_size"] for s in group)
            aliases = sorted(s.name for s in group)
            name = min(aliases, key=lambda n: (n.startswith("func_"), len(n), n))
            key = f"{module}:{offset:08x}"
            node = {"id": key, "name": name, "aliases": aliases, "module": module, "offset": offset, "size": size,
                    "source": next((origins[n] for n in aliases if n in origins), None),
                    "native_active": next((RECOVERED[n] for n in aliases if n in RECOVERED), None)}
            require(offset >= 0 and offset + size <= len(data[section_index]) and size % 4 == 0, "Function outside ELF section")
            raw = data[section_index][offset:offset + size]
            start = rom_offsets[section_index] + offset
            require(tool.rom[start:start + size] == raw, "Function bytes differ from ROM: " + name)
            bytes_checked += size
            nodes[key] = node; exact[(module, offset)] = key
            for alias in aliases:
                require(alias not in by_name or by_name[alias] == key, "Ambiguous function name: " + alias)
                by_name[alias] = key
        starts = {m: sorted(off for mod, off in exact if mod == m) for m in module_sections}

        def destination(module, offset):
            if (module, offset) in exact:
                return exact[module, offset], 0
            positions = starts.get(module, [])
            position = bisect.bisect_right(positions, offset) - 1
            if position >= 0:
                key = exact[module, positions[position]]
                if offset < nodes[key]["offset"] + nodes[key]["size"]:
                    return key, offset - nodes[key]["offset"]
            return None, None

        # Original main table: four-byte count, then ordinary relocation records.
        begin = named["mainRelocTable_ROM_START"]["st_value"]
        end = named["mainRelocTable_ROM_END"]["st_value"]
        count = struct.unpack_from(">I", tool.rom, begin)[0]
        require(4 + count * 8 <= end - begin, "Main relocation table outside ROM range")
        main_relocs = [RelocationEntry.from_bytes(tool.rom[begin + 4 + i * 8:begin + 12 + i * 8]) for i in range(count)]
        main_bias = tool.offsets["base_addr"] - sections[module_sections[0]]["sh_addr"]
        relocs = {0: {x.target_offset + main_bias: x for x in main_relocs if x.patch_type == 4}}
        for module in module_sections:
            if module:
                entries = [x for secondary in (False, True) for x in tool.get_relocation_entries(module, secondary=secondary)
                           if x.patch_type == 4]
                relocs[module] = {x.target_offset: x for x in entries}
                require(len(relocs[module]) == len(entries), "Duplicate jump relocation")

        edges, indirect, unresolved = [], [], []
        for key, node in nodes.items():
            module, offset = node["module"], node["offset"]
            section_index = module_sections[module]
            for local in range(0, node["size"], 4):
                site = offset + local
                word = struct.unpack_from(">I", data[section_index], site)[0]
                opcode = word >> 26
                if opcode == 0 and ((word & 63) == 9 or ((word & 63) == 8 and (word >> 21) & 31 != 31)):
                    indirect.append({"caller": key, "offset": local, "kind": "indirect_call" if word & 63 == 9 else "indirect_jump",
                                     "note": "Register target unresolved; jumps can include switch dispatch"})
                    continue
                if opcode not in (2, 3):
                    continue
                reloc = relocs[module].get(site)
                target_module, target_offset = module, (word & 0x3FFFFFF) << 2
                route = "direct"
                if reloc:
                    route = "main_relocation" if module == 0 else "overlay_relocation"
                    if reloc.reloc_type == 0:
                        target_module, target_offset = tool.resolve_ort_entry(reloc.symbol_index)
                        if target_module == 0:
                            target_offset += main_bias
                    elif reloc.reloc_type == 1:
                        target_offset = reloc.symbol_index
                    elif reloc.reloc_type != 2:
                        unresolved.append({"caller": key, "offset": local, "reason": "Unsupported jump relocation type"})
                        continue
                elif module == 0:
                    pc = sections[section_index]["sh_addr"] + site
                    target_offset = (((pc + 4) & 0xF0000000) | target_offset) - sections[section_index]["sh_addr"]
                target, interior = destination(target_module, target_offset)
                if opcode == 2 and target_module == module and offset <= target_offset < offset + node["size"]:
                    continue
                edge = {"caller": key, "offset": local, "kind": "call" if opcode == 3 else "tail_jump", "route": route,
                        "target": target, "target_module": target_module, "target_offset": target_offset, "interior_offset": interior}
                edges.append(edge)
                if target is None:
                    unresolved.append({**edge, "reason": "Target has no identified function extent"})
        # Sanity anchors whose dynamic routes were independently read in source.
        anchors = (("mainInitGame", 0xFC, "mainInitRlo"), ("controlPlayer", 0x660, "boyControl"))
        for caller, offset, target in anchors:
            require(any(x["caller"] == by_name[caller] and x["offset"] == offset and x["target"] == by_name[target] for x in edges),
                    "Known original linkage anchor failed: " + caller)
        require(any(x["caller"] == by_name["func_800457F4"] and x["target"] == by_name["objObjectsTick"] for x in edges),
                "Original game update no longer reaches object tick")

        outgoing = defaultdict(list)
        for edge in edges:
            outgoing[edge["caller"]].append(edge)
        roots = {}
        for name in ROOT_NAMES:
            require(name in by_name, "Missing program root: " + name)
            root = by_name[name]; visited = {root}; queue = deque([root])
            while queue:
                for edge in outgoing[queue.popleft()]:
                    target = edge["target"]
                    if target and target not in visited:
                        visited.add(target); queue.append(target)
            roots[name] = {"function": root, "static_reachable_functions": len(visited),
                           "reachable_modules": sorted({nodes[x]["module"] for x in visited}),
                           "unresolved_indirect_sites": sum(x["caller"] in visited for x in indirect),
                           "direct_callees": [{"offset": hex(x["offset"]), "name": nodes[x["target"]]["name"] if x["target"] else None,
                                               "module": x["target_module"], "route": x["route"], "interior_offset": x["interior_offset"]}
                                              for x in outgoing[root]]}
        modules = []
        for module in sorted(module_sections):
            members = [x for x in nodes.values() if x["module"] == module]
            destinations = Counter(x["target_module"] for x in edges if nodes[x["caller"]]["module"] == module and x["target_module"] != module)
            modules.append({"module": module, "functions": len(members), "sized_functions": sum(x["size"] > 0 for x in members),
                            "function_bytes": sum(x["size"] for x in members), "cross_module_edges": dict(sorted(destinations.items())),
                            "entry_names": [x["name"] for x in members[:4]]})
        summary = {"status": "audited_static", "rom_sha1": hashlib.sha1(tool.rom).hexdigest(), "function_entries": len(nodes),
                   "sized_functions": sum(x["size"] > 0 for x in nodes.values()), "zero_size_entries": sum(x["size"] == 0 for x in nodes.values()),
                   "bytes_compared_to_rom": bytes_checked, "modules": len(modules), "direct_edges": len(edges),
                   "main_relocation_records": count, "edge_routes": dict(Counter(x["route"] for x in edges)),
                   "offset_origin": "ELF section start; main relocation/ORT code base is 0x50 bytes after .main start",
                   "indirect_sites": len(indirect), "unresolved_direct_targets": len(unresolved),
                   "indirect_kinds": dict(Counter(x["kind"] for x in indirect)),
                   "rom_module_slots_without_elf_section": sorted(set(range(tool.offsets["num_overlays"] + 1)) - set(module_sections)),
                   "recovered_native_decisions": RECOVERED, "roots": roots, "module_inventory": modules,
                   "limits": ["Static graph over ELF function extents, not executed control flow or a completion percentage",
                              "Register-indirect calls/jumps, jump-table targets and data-driven callback reachability remain incomplete",
                              "Zero-size entries and interior targets need explicit boundaries before porting",
                              "Native decision mappings cover only their documented scope; legacy recompilation is not active integration",
                              "No MIPS, console device or emulator execution"]}
        return {"summary": summary, "functions": list(nodes.values()), "edges": edges, "indirect_sites": indirect, "unresolved": unresolved}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-native/integration")
    args = parser.parse_args()
    out = args.out.resolve()
    require(out.is_relative_to(ROOT / "build"), "Generated inventory must stay under build/")
    result = inventory(args.elf, args.rom)
    out.mkdir(parents=True, exist_ok=True)
    (out / "program-map.json").write_text(json.dumps(result, indent=2) + "\n")
    (out / "program-map-summary.json").write_text(json.dumps(result["summary"], indent=2) + "\n")
    print(json.dumps({k: v for k, v in result["summary"].items() if k not in ("roots", "module_inventory", "limits", "recovered_native_decisions")}))


if __name__ == "__main__":
    main()
