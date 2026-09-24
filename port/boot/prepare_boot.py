#!/usr/bin/env python3
"""Create a separate profile for original heap/linker startup and module loads."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import sys

from elftools.elf.elffile import ELFFile

from reference import ROOT, SYMBOLS, PLATFORM_IMPORTS, profile, return_address_patches
from prepare import FUNCTIONS, prepare, sized_function
from overlay_reloc import OverlayRelocTool

ROOTS = ("RevealReturnAddresses", "mmInit", "mmAlloc2", "runlinkInitialise",
         "runlinkDownloadCode", "runlinkUnloadOverlay", "runlinkIsModuleLoaded")
CALLBACKS = ("_AutoInit00044", "amAudioLinesReset")
# These branches stop previously playing sounds. Fresh module initialization
# has no playing sounds. They remain explicit unresolved dependencies, not
# functions pretending to complete an audio operation.
DEFERRED = ("amSndStop", "amAmbientStop")


def closure(elf_path, roots=ROOTS, host_imports=PLATFORM_IMPORTS, deferred=(), function_ends=None,
            rom_path=None):
    with elf_path.open("rb") as file:
        elf = ELFFile(file)
        sections = list(elf.iter_sections())
        symtab = elf.get_section_by_name(".symtab")
        symbols = [sized_function(symtab, s.name, function_ends) if s.name in (function_ends or {}) else s
                   for s in symtab.iter_symbols()
                   if s["st_info"]["type"] == "STT_FUNC" and isinstance(s["st_shndx"], int)
                   and (s["st_size"] or s.name in (function_ends or {}))]
        by_name = {}
        for symbol in symbols:
            if symbol.name in by_name and (symbol["st_value"], symbol["st_shndx"]) != (
                    by_name[symbol.name]["st_value"], by_name[symbol.name]["st_shndx"]):
                raise ValueError(f"Ambiguous function name: {symbol.name}")
            by_name[symbol.name] = symbol
        by_address = {}
        for symbol in symbols:
            by_address.setdefault((symbol["st_shndx"], symbol["st_value"]), []).append(symbol)
        overlay_sections = {int(section.name.removeprefix(".overlay_")): index
                            for index, section in enumerate(sections)
                            if section.name.startswith(".overlay_") and section.name[9:].isdigit()}
        main_section = next((index for index, section in enumerate(sections) if section.name == ".main"), None)
        reloc_tool = OverlayRelocTool(str(rom_path)) if rom_path is not None else None
        reloc_cache = {}

        def target_symbol(section_index, address, caller):
            candidates = by_address.get((section_index, address), [])
            names = {candidate.name for candidate in candidates}
            if len(names) != 1:
                raise ValueError(f"Unresolved or ambiguous static dependency at {address:08X} from {caller}")
            return next(iter(names))

        def overlay_relocations(number):
            if number not in reloc_cache:
                if reloc_tool is None:
                    raise ValueError(f"rom_path is required to resolve overlay {number} calls")
                entries = {}
                for secondary in (False, True):
                    for entry in reloc_tool.get_relocation_entries(number, secondary=secondary):
                        if entry.patch_type == 4:
                            if entry.target_offset in entries:
                                raise ValueError(f"Ambiguous JAL relocation in overlay {number} at {entry.target_offset:X}")
                            entries[entry.target_offset] = entry
                reloc_cache[number] = entries
            return reloc_cache[number]

        todo, seen = list(roots), set()
        data_cache = {}
        while todo:
            name = todo.pop()
            if name in seen:
                continue
            seen.add(name)
            if name in host_imports or name in deferred:
                continue
            symbol = by_name[name]
            section = sections[symbol["st_shndx"]]
            overlay = int(section.name[9:]) if section.name.startswith(".overlay_") and section.name[9:].isdigit() else 0
            relocs = overlay_relocations(overlay) if overlay else {}
            if section.name not in data_cache:
                data_cache[section.name] = section.data()
            data = data_cache[section.name]
            offset = symbol["st_value"] - section["sh_addr"]
            for index, (word,) in enumerate(struct.iter_unpack(">I", data[offset:offset + symbol["st_size"]])):
                if word >> 26 not in (2, 3):
                    continue
                site = offset + index * 4
                entry = relocs.get(site)
                if entry is not None:
                    if entry.reloc_type == 0:
                        target_overlay, target_offset = reloc_tool.resolve_ort_entry(entry.symbol_index)
                        if target_overlay == 0:
                            target_section = main_section
                        else:
                            target_section = overlay_sections.get(target_overlay)
                        if target_section is None:
                            raise ValueError(f"Missing overlay {target_overlay} for JAL from {name}")
                        base = (reloc_tool.offsets["base_addr"] if target_overlay == 0
                                else sections[target_section]["sh_addr"])
                        target = base + target_offset
                    elif entry.reloc_type == 1:
                        target_section = symbol["st_shndx"]
                        target = section["sh_addr"] + entry.symbol_index
                    elif entry.reloc_type == 2:
                        target_section = symbol["st_shndx"]
                        target = section["sh_addr"] + ((word & 0x03FFFFFF) << 2)
                    else:
                        raise ValueError(f"Unsupported JAL relocation from {name}: {entry}")
                else:
                    target_section = symbol["st_shndx"] if overlay else main_section
                    if overlay:
                        # Unrelocated overlay jumps use section-relative fields.
                        target = section["sh_addr"] + ((word & 0x03FFFFFF) << 2)
                    else:
                        target = ((symbol["st_value"] + index * 4 + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                if word >> 26 == 2 and target_section == symbol["st_shndx"] and symbol["st_value"] <= target < symbol["st_value"] + symbol["st_size"]:
                    continue
                todo.append(target_symbol(target_section, target, name))
        return sorted(seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-boot/proof")
    args = parser.parse_args()
    state = profile(args.elf, args.rom)
    patches = return_address_patches(state)
    names = tuple(dict.fromkeys((*FUNCTIONS, *closure(args.elf, rom_path=args.rom), *CALLBACKS, *DEFERRED)))
    manifest = prepare(args.elf, args.rom, args.out, functions=names,
                       host_imports=PLATFORM_IMPORTS, deferred=DEFERRED,
                       extra_symbols=SYMBOLS, runtime_patches=patches)
    manifest["boot_profile"] = {"stack_top": state["stack_top"], "platform_imports": PLATFORM_IMPORTS,
                                "deferred_audio_branches": DEFERRED, "roots": ROOTS,
                                "callbacks": CALLBACKS, "loaded_modules": [19, 6, 32, 44]}
    manifest["limits"] = ["Heap/linker initialization slice, not full boot or mainThread",
                           "Synchronous ROM reads and coherent-host cache contracts",
                           "Two explicit deferred audio-stop paths; invoking them fails",
                           "Single-threaded; no rendering, controller or audio output"]
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Boot profile: {len(names)} entries, {len(patches)} runtime instruction patches, {len(PLATFORM_IMPORTS)} platform imports.")


if __name__ == "__main__":
    main()
