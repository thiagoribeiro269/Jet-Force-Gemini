#!/usr/bin/env python3
"""Export a deliberately small JFG US CPU proof from a verified local ROM/ELF.

Only metadata and this tooling belong in Git. Recompiled game code and input
binaries are generated under the ignored build directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from overlay_reloc import OverlayRelocTool  # noqa: E402

ROM_SHA1 = "493ced9008dbe932d6e91179b68e8630cf23a023"
FUNCTIONS = (
    "mainGetZBCheck", "mainGameWindowChanging", "mainSetAnimGroup",
    "mainGetAnimGroup", "mainChangeCameras", "mainGetNextCharacter",
    "mainGetNextLevel", "mainSyncNextLevel", "mainSetMode", "mainGetGame",
    "mainGetCurrentLevel", "mainGetGameArrayPtr", "mainGetNumberOfCameras",
    "GetRegionIndex", "pauseGetScreen", "pauseGetPauseCharacter",
    "frontSetInstrumentsHide", "func_overlay_18_0120207C_1F31A54",
    "func_overlay_32_02001D2C_1F526FC", "func_overlay_32_02001D84_1F52754",
)
REFERENCE_SYMBOLS = (
    "ProcessRelocationEntry", "ResolveRelocAddress", "PatchInstruction",
    "overlayTable", "overlayRomTable", "gRelocTextBase", "gRelocDataBase",
    "__CODE_SECTION_START", "__DATA_SECTION_START", "__BSS_SECTION_START", "__BSS_SECTION_END",
    "mainGameMode", "gameplay",
)


def unique_symbol(symtab, name):
    candidates = symtab.get_symbol_by_name(name) or []
    values = {(s["st_value"], s["st_size"], s["st_shndx"]) for s in candidates}
    if len(values) != 1:
        raise ValueError(f"Expected one unambiguous ELF symbol: {name}")
    return candidates[0]


def sized_function(symtab, name, function_ends=None):
    symbol = unique_symbol(symtab, name)
    if name in (function_ends or {}):
        end = unique_symbol(symtab, function_ends[name])
        if (symbol["st_size"] or symbol["st_info"]["type"] != "STT_FUNC" or
                end["st_info"]["type"] != "STT_FUNC" or end["st_shndx"] != symbol["st_shndx"] or
                end["st_value"] <= symbol["st_value"]):
            raise ValueError(f"Invalid explicit function boundary: {name}")
        for other in symtab.iter_symbols():
            if (other["st_info"]["type"] == "STT_FUNC" and other["st_shndx"] == symbol["st_shndx"] and
                    symbol["st_value"] < other["st_value"] < end["st_value"]):
                raise ValueError(f"Function boundary crosses {other.name}: {name}")
        symbol.entry["st_size"] = end["st_value"] - symbol["st_value"]
    return symbol


def rom_offset(elf, section):
    candidates = [
        p["p_paddr"] + section["sh_offset"] - p["p_offset"]
        for p in elf.iter_segments()
        if p["p_type"] == "PT_LOAD" and p.section_in_segment(section)
    ]
    if len(candidates) != 1:
        raise ValueError(f"Ambiguous ROM mapping for {section.name}")
    return candidates[0]


def convert_relocations(tool, number, section, selected, rom, sections, reference):
    """Translate proven HI16/LO16 pairs and external/local JALs.

    The oracle independently executes the original linker for every group.
    Other forms and unlisted dependencies remain explicit errors.
    """
    converted, groups = [], []
    by_overlay = {s["overlay"]: s for s in sections}
    ranges = [(f["offset"], f["offset"] + f["size"]) for f in selected
              if not f["host_import"] and not f["deferred"]]

    def resolve(entry, addend):
        if entry.reloc_type == 1:
            return section, entry.symbol_index + addend
        if entry.reloc_type != 0:
            raise ValueError(f"Unsupported relocation source: {entry}")
        target_overlay, symbol_offset = tool.resolve_ort_entry(entry.symbol_index)
        anchors = {0: "__CODE_SECTION_START", 0xFFD: "__DATA_SECTION_START",
                   0xFFE: "__DATA_SECTION_START", 0xFFF: "__BSS_SECTION_START"}
        if target_overlay in anchors:
            target = by_overlay[0]
            offset = reference[anchors[target_overlay]] + symbol_offset - target["vram"] + addend
        else:
            if target_overlay not in by_overlay:
                raise ValueError(f"Missing overlay dependency {target_overlay}: {tool.get_symbol_name(entry.symbol_index)}")
            target = by_overlay[target_overlay]
            offset = symbol_offset + addend
        return target, offset

    for secondary in (False, True):
        entries = tool.get_relocation_entries(number, secondary=secondary)
        index = 0
        while index < len(entries):
            entry = entries[index]
            if not any(lo <= entry.target_offset < hi for lo, hi in ranges):
                index += 1
                continue
            group = {"table": "secondary" if secondary else "primary", "index": index,
                     "symbol_index": entry.symbol_index, "source_type": entry.reloc_type}
            if entry.reloc_type in (0, 2) and entry.patch_type == 4:
                word = struct.unpack_from(">I", rom, section["rom"] + entry.target_offset)[0]
                if word >> 26 != 3:
                    raise ValueError("This proof supports JAL relocation, not other jump forms")
                if entry.reloc_type == 2:
                    target, target_offset = section, (word & 0x03FFFFFF) << 2
                else:
                    target, target_offset = resolve(entry, 0)
                callee = next((f for f in target["functions"] if f["offset"] == target_offset), None)
                if callee is None:
                    name = "local" if entry.reloc_type == 2 else tool.get_symbol_name(entry.symbol_index)
                    raise ValueError(f"Unlisted function dependency: {name}")
                converted.append({"vram": section["vram"] + entry.target_offset,
                                  "target_vram": target["vram"] + target_offset,
                                  "target_section": target["index"], "type": "R_MIPS_26"})
                group.update(kind="call", consumed=1, patch_offset=entry.target_offset,
                             target_section=target["index"], target_offset=target_offset, callee=callee["name"])
                groups.append(group)
                index += 1
                continue
            if entry.reloc_type not in (0, 1) or entry.patch_type != 5 or index + 1 >= len(entries):
                raise ValueError(f"Unsupported proof relocation in overlay {number}: {entry}")
            low = entries[index + 1]
            if (low.reloc_type, low.patch_type, low.symbol_index) != (entry.reloc_type, 6, entry.symbol_index):
                raise ValueError(f"Invalid HI16/LO16 pair in overlay {number}")
            if not any(lo <= low.target_offset < hi for lo, hi in ranges):
                raise ValueError("Relocation pair crosses the selected function boundary")
            hi_word = struct.unpack_from(">I", rom, section["rom"] + entry.target_offset)[0]
            lo_word = struct.unpack_from(">I", rom, section["rom"] + low.target_offset)[0]
            if hi_word >> 26 != 0x0F:
                raise ValueError("HI16 relocation does not refer to a LUI instruction")
            signed_low = struct.unpack(">h", struct.pack(">H", lo_word & 0xFFFF))[0]
            target, target_offset = resolve(entry, ((hi_word & 0xFFFF) << 16) + signed_low)
            # JFG's audio-line loop forms an end pointer one word beyond BSS;
            # ADDIU forms an address and does not dereference that sentinel.
            address_only_end = lo_word >> 26 == 9 and target["memory_size"] <= target_offset <= target["memory_size"] + 4
            if not (0 <= target_offset < target["memory_size"] or address_only_end):
                raise ValueError(f"Relocation target is outside section memory: {section['name']} +{entry.target_offset:X} -> {target_offset:X}/{target['memory_size']:X}")
            for rel, kind in ((entry, "R_MIPS_HI16"), (low, "R_MIPS_LO16")):
                converted.append({"vram": section["vram"] + rel.target_offset,
                                  "target_vram": target["vram"] + target_offset,
                                  "target_section": target["index"],
                                  "type": kind})
            group.update(kind="pair", consumed=2, hi_offset=entry.target_offset,
                         lo_offset=low.target_offset, target_offset=target_offset,
                         target_section=target["index"], address_only_end=address_only_end)
            groups.append(group)
            index += 2
    converted.sort(key=lambda rel: rel["vram"])
    if len({rel["vram"] for rel in converted}) != len(converted):
        raise ValueError("Duplicate relocation sites require a separate runtime analysis")
    return converted, groups


def prepare(elf_path: Path, rom_path: Path, out: Path, *, functions=FUNCTIONS,
            host_imports=(), deferred=(), extra_symbols=(), runtime_patches=(), function_ends=None):
    rom = rom_path.read_bytes()
    if len(rom) != 0x2000000 or hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise ValueError("The proof requires the verified, unmodified US ROM")
    out.mkdir(parents=True, exist_ok=True)
    tool = OverlayRelocTool(str(rom_path))
    sections, output_functions = [], []
    with elf_path.open("rb") as file:
        elf = ELFFile(file)
        if elf.elfclass != 32 or elf.little_endian or elf["e_machine"] != "EM_MIPS":
            raise ValueError("Expected the big-endian MIPS ELF produced by the N64 build")
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise ValueError("ELF symbol table is missing")
        by_name = {}
        for name in functions:
            symbol = sized_function(symtab, name, function_ends)
            if symbol["st_info"]["type"] != "STT_FUNC" or not symbol["st_size"]:
                raise ValueError(f"Invalid function symbol: {name}")
            section = elf.get_section(symbol["st_shndx"])
            if section.name not in by_name:
                number = int(section.name.removeprefix(".overlay_")) if section.name.startswith(".overlay_") else 0
                info = {"name": section.name, "index": len(sections), "overlay": number,
                        "vram": section["sh_addr"], "rom": rom_offset(elf, section),
                        "size": section["sh_size"], "functions": []}
                data = section.data()
                if data != rom[info["rom"]:info["rom"] + len(data)]:
                    raise ValueError(f"ELF section {section.name} differs from the reference ROM")
                if number:
                    header = tool.get_overlay_header(number)
                    if tool.offsets["overlay_data_base"] + header.rom_offset != info["rom"]:
                        raise ValueError("ELF and game overlay tables disagree")
                    info["header"] = vars(header)
                    info["load_size"] = header.text_size + header.data_size
                    info["memory_size"] = info["load_size"] + header.bss_size
                else:
                    info["load_size"] = info["size"]
                    info["memory_size"] = unique_symbol(symtab, "__BSS_SECTION_END")["st_value"] - info["vram"]
                by_name[section.name] = info
                sections.append(info)
            info = by_name[section.name]
            offset = symbol["st_value"] - info["vram"]
            if offset < 0 or offset + symbol["st_size"] > info["load_size"]:
                raise ValueError(f"Function {name} is outside the section")
            function = {"name": name, "vram": symbol["st_value"], "size": symbol["st_size"],
                        "section": info["index"], "offset": offset,
                        "rom": info["rom"] + offset,
                        "host_import": name in host_imports, "deferred": name in deferred,
                        "binding": "jfg_host_" + name if name in host_imports else name}
            if name in (function_ends or {}):
                function["end_symbol"] = function_ends[name]
            info["functions"].append(function)
            output_functions.append(function)
        reference = {name: unique_symbol(symtab, name)["st_value"] for name in dict.fromkeys((*REFERENCE_SYMBOLS, *extra_symbols))}
        function_count = sum(s["st_info"]["type"] == "STT_FUNC" for s in symtab.iter_symbols())

    toml = []
    for patch in runtime_patches:
        toml.extend(["[[runtime_patch]]", f'vram = 0x{patch["vram"]:X}',
                     f'before = 0x{patch["before"]:X}', f'after = 0x{patch["after"]:X}', ""])
    for section in sections:
        relocs, groups = convert_relocations(tool, section["overlay"], section, section["functions"], rom,
                                             sections, reference) if section["overlay"] else ([], [])
        section["relocations"] = relocs
        section["relocation_groups"] = groups
        toml.extend(["[[section]]", f'name = {json.dumps(section["name"])}',
                     f'rom = 0x{section["rom"]:X}', f'vram = 0x{section["vram"]:X}',
                     f'size = 0x{section["size"]:X}', "functions = ["])
        for function in section["functions"]:
            toml.append('  { name = "%s", vram = 0x%X, size = 0x%X, host_import = %s, deferred = %s },' %
                        (function["name"], function["vram"], function["size"],
                         str(function["host_import"]).lower(), str(function["deferred"]).lower()))
        toml.append("]")
        toml.append("relocs = [")
        for rel in relocs:
            toml.append('  { vram = 0x%X, target_vram = 0x%X, target_section = %d, type = "%s" },' %
                        (rel["vram"], rel["target_vram"], rel["target_section"], rel["type"]))
        toml.append("]")
        toml.append("")
    (out / "symbols.toml").write_text("\n".join(toml), encoding="utf-8")
    config = {"symbols_file_path": "symbols.toml", "rom_file_path": rom_path.resolve().as_posix(),
              "output_func_path": "generated", "functions_per_output_file": 50}
    (out / "recomp.toml").write_text("[input]\n" + "\n".join(
        f"{key} = {json.dumps(value)}" for key, value in config.items()) + "\n", encoding="utf-8")
    header = ["/* Generated metadata only; do not edit. */", "#pragma once",
              f"#define JFG_POC_SECTION_COUNT {len(sections)}",
              f"#define JFG_POC_FUNCTION_COUNT {len(output_functions)}"]
    for function in output_functions:
        if not function["deferred"]:
            header.append(f'extern void {function["binding"]}(uint8_t*, recomp_context*);')
    header.append("static const struct PocFunction poc_functions[] = {")
    for function in output_functions:
        header.append('    { "%s", %s, %d, 0x%Xu },' %
                      (function["name"], "NULL" if function["deferred"] else function["binding"], function["section"], function["offset"]))
    header.append("};")
    header.append("static const struct PocSection poc_sections[] = {")
    for section in sections:
        header.append('    { 0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu, %du },' %
                      (section["rom"], section["load_size"], section["memory_size"],
                       0 if section["overlay"] else section["vram"], section["overlay"]))
    header.append("};")
    (out / "poc_symbols.h").write_text("\n".join(header) + "\n", encoding="utf-8")
    main_section = next(s for s in sections if not s["overlay"])
    (out / "poc_smoke.h").write_text(
        "#pragma once\n"
        f'#define POC_MAIN_SECTION {main_section["index"]}u\n'
        f'#define POC_MAIN_BASE 0x{main_section["vram"]:08X}u\n'
        f'#define POC_GAME_MODE 0x{reference["mainGameMode"]:08X}u\n', encoding="utf-8")
    manifest = {"rom_sha1": ROM_SHA1, "rom_path": rom_path.resolve().as_posix(),
                "elf_path": elf_path.resolve().as_posix(), "elf_function_symbols": function_count,
                "sections": sections, "functions": output_functions, "reference_symbols": reference,
                "runtime_patches": list(runtime_patches),
                "overlay_rom_table": tool.offsets["overlay_rom_table"],
                "overlay_table": tool.offsets["overlay_table"],
                "overlay_data_base": tool.offsets["overlay_data_base"],
                "limits": ["Selected integer functions only", "Selected local/external HI16/LO16 and JAL relocations",
                           "No full runLink loader, boot, graphics, audio or gameplay"]}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(output_functions)} function entries in {len(sections)} sections; {sum(len(s['relocation_groups']) for s in sections)} real relocation groups.")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-recomp/proof")
    args = parser.parse_args()
    prepare(args.elf, args.rom, args.out)


if __name__ == "__main__":
    main()
