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

ROOTS = ("RevealReturnAddresses", "mmInit", "mmAlloc2", "runlinkInitialise",
         "runlinkDownloadCode", "runlinkUnloadOverlay", "runlinkIsModuleLoaded")
CALLBACKS = ("_AutoInit00044", "amAudioLinesReset")
# These branches stop previously playing sounds. Fresh module initialization
# has no playing sounds. They remain explicit unresolved dependencies, not
# functions pretending to complete an audio operation.
DEFERRED = ("amSndStop", "amAmbientStop")


def closure(elf_path, roots=ROOTS, host_imports=PLATFORM_IMPORTS, deferred=(), function_ends=None):
    with elf_path.open("rb") as file:
        elf = ELFFile(file)
        sections = list(elf.iter_sections())
        symtab = elf.get_section_by_name(".symtab")
        symbols = [sized_function(symtab, s.name, function_ends) if s.name in (function_ends or {}) else s
                   for s in symtab.iter_symbols()
                   if s["st_info"]["type"] == "STT_FUNC" and isinstance(s["st_shndx"], int)
                   and (s["st_size"] or s.name in (function_ends or {}))]
        by_name = {s.name: s for s in symbols}
        by_address = {}
        for symbol in symbols:
            by_address.setdefault(symbol["st_value"], []).append(symbol)
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
            if section.name not in data_cache:
                data_cache[section.name] = section.data()
            data = data_cache[section.name]
            offset = symbol["st_value"] - section["sh_addr"]
            for index, (word,) in enumerate(struct.iter_unpack(">I", data[offset:offset + symbol["st_size"]])):
                if word >> 26 not in (2, 3):
                    continue
                target = ((symbol["st_value"] + index * 4 + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                if word >> 26 == 2 and symbol["st_value"] <= target < symbol["st_value"] + symbol["st_size"]:
                    continue
                candidates = by_address.get(target, [])
                if len(candidates) != 1:
                    raise ValueError(f"Unresolved static dependency at {target:08X} from {name}")
                todo.append(candidates[0].name)
        return sorted(seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "build/port-boot/proof")
    args = parser.parse_args()
    state = profile(args.elf, args.rom)
    patches = return_address_patches(state)
    names = tuple(dict.fromkeys((*FUNCTIONS, *closure(args.elf), *CALLBACKS, *DEFERRED)))
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
