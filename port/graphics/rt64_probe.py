#!/usr/bin/env python3
"""Reproduce RT64's pinned GBI identification against the private JFG US ROM.

This checks the CPU-side hash lookup only. It does not execute RT64 or a GPU.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import re

from elftools.elf.elffile import ELFFile


ROOT = Path(__file__).resolve().parents[2]
PINNED_RT64 = "43373749dac9bbc1b653e6a02aed40a9e1783bed"
ROM_SHA1 = "493ced9008dbe932d6e91179b68e8630cf23a023"
GBI_CPP_SHA256 = "ecb1dc7bb915576ba2fe4116fee0c8de9d4c696e2d5f6c87c67491335e40ae53"
GBI_H_SHA256 = "38b849582be5f674c6e9522433a0b5488ebda53d639837ffd73ede31efeeb6da"
ROM_STARTS = {"boot": 0x9FE50, "text": 0x9FFD0, "data": 0xB0F50}
SEGMENT_RE = re.compile(
    r"GBISegment\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*"
    r"(0x[0-9A-Fa-f]+)ULL\s*,\s*\{\s*([^}]*)\}\s*\}"
)
INSTANCE_RE = re.compile(r"&([A-Za-z_][A-Za-z_0-9]*)")
TABLE_RE = re.compile(
    r"static std::array<GBISegment,\s*(\d+)>\s+"
    r"(textSegments|dataSegments)\s*=\s*\{(.*?)\n\s*\};", re.S
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_tables(source: str) -> dict[str, list[dict]]:
    tables = {}
    for declared, name, body in TABLE_RE.findall(source):
        entries = []
        for length, hash_value, names in SEGMENT_RE.findall(body):
            instances = INSTANCE_RE.findall(names)
            if not instances or re.sub(r"&[A-Za-z_][A-Za-z_0-9]*|[\s,]", "", names):
                raise ValueError(f"Unexpected instance syntax in {name}")
            entries.append({"length": int(length, 0), "hash": int(hash_value, 16),
                            "instances": instances})
        leftover = SEGMENT_RE.sub("", body)
        if re.sub(r"//[^\n]*|\s|,", "", leftover):
            raise ValueError(f"Unparsed GBI segment in {name}")
        if len(entries) != int(declared):
            raise ValueError(f"{name}: expected {declared} rows, parsed {len(entries)}")
        tables[name] = sorted(entries, key=lambda entry: entry["length"])
    if set(tables) != {"textSegments", "dataSegments"}:
        raise ValueError("Both RT64 segment tables must be present")
    return tables


def word_swap(data: bytes) -> bytes:
    if len(data) % 4:
        raise ValueError("RDRAM image is not word aligned")
    return b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4))


def hashes_for_lengths(source: bytes, entries: list[dict], lib) -> tuple[list[dict], dict[int, str]]:
    """Hash ascending lengths, as GBIManager's sorted prefix scan does."""
    results, by_length = [], {}
    state = lib.XXH3_createState()
    if not state:
        raise RuntimeError("XXH3_createState failed")
    try:
        if lib.XXH3_64bits_reset(state) != 0:
            raise RuntimeError("XXH3_64bits_reset failed")
        cursor = 0
        for entry in entries:
            length = entry["length"]
            if length > len(source):
                raise ValueError(f"RT64 requested {length:#x} bytes beyond available main RDRAM")
            if length > cursor:
                chunk = source[cursor:length]
                if lib.XXH3_64bits_update(state, chunk, len(chunk)) != 0:
                    raise RuntimeError("XXH3_64bits_update failed")
                cursor = length
            digest = lib.XXH3_64bits_digest(state)
            by_length[length] = f"0x{digest:016X}"
            if digest == entry["hash"]:
                results.append({"length": length, "hash": by_length[length],
                                "instances": entry["instances"]})
    finally:
        lib.XXH3_freeState(state)
    return results, by_length


def run(args: argparse.Namespace) -> dict:
    rt64 = args.rt64_source
    import subprocess
    head = subprocess.check_output(["git", "-C", str(rt64), "rev-parse", "HEAD"], text=True).strip()
    if head != PINNED_RT64:
        raise ValueError(f"RT64 HEAD differs from pinned commit: {head}")
    cpp = rt64 / "src/gbi/rt64_gbi.cpp"
    header = rt64 / "src/gbi/rt64_gbi.h"
    if sha256(cpp) != GBI_CPP_SHA256 or sha256(header) != GBI_H_SHA256:
        raise ValueError("Pinned RT64 GBI sources differ from reviewed bytes")
    source = cpp.read_text()
    tables = parse_tables(source)
    if "F3DJFG" in source or "F3DDKR" in source or "F3DJFG" in header.read_text() or "F3DDKR" in header.read_text():
        raise ValueError("Unexpected JFG/DKR GBI support in pinned sources")

    rom = args.rom.read_bytes()
    if len(rom) != 0x2000000 or hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise ValueError("ROM is not the verified JFG US image")
    if rom[:4] != bytes.fromhex("80371240"):
        raise ValueError("ROM is not big-endian z64")
    with args.elf.open("rb") as stream:
        elf = ELFFile(stream)
        main = elf.get_section_by_name(".main")
        main_bss = elf.get_section_by_name(".main_bss")
        if main is None or main["sh_addr"] != 0x80000400:
            raise ValueError("Unexpected ELF .main section")
        if (main_bss is None or main_bss["sh_type"] != "SHT_NOBITS"
                or main_bss["sh_addr"] != main["sh_addr"] + main["sh_size"]):
            raise ValueError("ELF .main_bss must immediately follow .main")
        mappings = [segment["p_paddr"] + main["sh_offset"] - segment["p_offset"]
                    for segment in elf.iter_segments()
                    if segment["p_type"] == "PT_LOAD" and segment.section_in_segment(main)]
        if len(mappings) != 1 or mappings[0] != 0x1000:
            raise ValueError("Unexpected ELF .main ROM mapping")
        main_rom, main_data = mappings[0], main.data()
        if main_data != rom[main_rom:main_rom + len(main_data)]:
            raise ValueError("ELF .main bytes differ from verified ROM")

    starts = ROM_STARTS
    intervals = {"boot": (starts["boot"], starts["text"]),
                 "text": (starts["text"], 0xA1260),
                 "data": (starts["data"], main_rom + len(main_data))}
    expected_sizes = {"boot": 0x180, "text": 0x1290, "data": 0x800}
    for name, (start, end) in intervals.items():
        if end - start != expected_sizes[name] or not main_rom <= start < end <= main_rom + len(main_data):
            raise ValueError(f"Unexpected {name} microcode interval")
        if rom[start:end] != main_data[start - main_rom:end - main_rom]:
            raise ValueError(f"{name} interval differs between ROM and ELF")
    # RT64 hashes from the text/data RDRAM address onward. The static image is
    # useful for source analysis, but adjacent globals can change at runtime.
    if args.ram is not None:
        big_endian_ram = args.ram.read_bytes()
        if len(big_endian_ram) != 0x800000:
            raise ValueError("--ram must be an 8 MiB big-endian RDRAM snapshot")
        for name, (start, end) in intervals.items():
            physical = (main["sh_addr"] + start - main_rom) & 0x7FFFFF
            if big_endian_ram[physical:physical + end - start] != rom[start:end]:
                raise ValueError(f"Live RDRAM {name} microcode blob differs from verified ROM")
        rdram = word_swap(big_endian_ram)
        rdram_origin = "actual_8mib_snapshot"
        address_base = 0
    else:
        # `.main_bss` is initially zeroed. This is a read-only static
        # assumption and does not establish the game RAM's later contents.
        rdram = word_swap(main_data + bytes(main_bss["sh_size"]))
        rdram_origin = "initial_elf_main_and_zeroed_bss"
        address_base = main["sh_addr"] & 0x7FFFFF
    lib = ctypes.CDLL("libxxhash.so.0")
    lib.XXH_versionNumber.argtypes = ()
    lib.XXH_versionNumber.restype = ctypes.c_uint
    lib.XXH3_createState.argtypes = ()
    lib.XXH3_createState.restype = ctypes.c_void_p
    lib.XXH3_64bits_reset.argtypes = (ctypes.c_void_p,)
    lib.XXH3_64bits_reset.restype = ctypes.c_int
    lib.XXH3_64bits_update.argtypes = (ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t)
    lib.XXH3_64bits_update.restype = ctypes.c_int
    lib.XXH3_64bits_digest.argtypes = (ctypes.c_void_p,)
    lib.XXH3_64bits_digest.restype = ctypes.c_uint64
    lib.XXH3_freeState.argtypes = (ctypes.c_void_p,)
    lib.XXH3_freeState.restype = ctypes.c_int
    matches = {}
    all_hashes = {}
    for kind, table in (("text", "textSegments"), ("data", "dataSegments")):
        start = ((main["sh_addr"] + starts[kind] - main_rom) & 0x7FFFFF) - address_base
        matches[kind], all_hashes[kind] = hashes_for_lengths(rdram[start:], tables[table], lib)
    shared = sorted({instance for text in matches["text"] for data in matches["data"]
                     for instance in set(text["instances"]) & set(data["instances"])})
    # RT64's loop overwrites the index on every matching row, then intersects
    # only the last text and data rows. Keep that behavior distinct from all matches.
    final_text = matches["text"][-1] if matches["text"] else None
    final_data = matches["data"][-1] if matches["data"] else None
    actual = sorted(set(final_text["instances"]) & set(final_data["instances"])) if final_text and final_data else []
    return {
        "test": "rt64_gbi_identification_only", "rt64_commit": PINNED_RT64,
        "rt64_source_sha256": {"rt64_gbi.cpp": GBI_CPP_SHA256, "rt64_gbi.h": GBI_H_SHA256},
        "rom_sha1": ROM_SHA1,
        "xxhash_version_number": lib.XXH_versionNumber(),
        "hash_memory_origin": rdram_origin,
        "ram_snapshot_sha256": hashlib.sha256(big_endian_ram).hexdigest() if args.ram is not None else None,
        "snapshot_microcode_equals_rom": True if args.ram is not None else None,
        "intervals": {name: {"rom_start": f"0x{start:X}", "rom_end_exclusive": f"0x{end:X}",
                             "vram_start": f"0x{main['sh_addr'] + start - main_rom:X}", "size": end - start,
                             "sha256": hashlib.sha256(rom[start:end]).hexdigest(),
                             "rom_equals_elf": True}
                      for name, (start, end) in intervals.items()},
        "rdram_layout": "word-swapped (each big-endian ROM word reversed)",
        "tables": {name: {"rows": len(rows), "lengths": sorted({entry["length"] for entry in rows})}
                   for name, rows in tables.items()},
        "text_prefix_extends_beyond_named_blob": max(map(int, all_hashes["text"])) > expected_sizes["text"],
        "data_prefix_extends_beyond_named_blob": max(map(int, all_hashes["data"])) > expected_sizes["data"],
        "static_image_caveat": ("Long prefixes include adjacent .main globals and initial zeroed .main_bss; "
                                "these may differ during gameplay" if args.ram is None else None),
        "prefix_hashes_xxh3_64": {kind: {f"0x{length:X}": digest for length, digest in hashes.items()}
                                   for kind, hashes in all_hashes.items()},
        "matches": matches, "all_matching_instance_intersection": shared,
        "rt64_selected_text_row": final_text, "rt64_selected_data_row": final_data,
        "rt64_selected_instance_intersection": actual,
        "rt64_gbi_lookup_status": ("matched" if actual else "unsupported") if args.ram is not None
                                  else ("matched_initial_image" if actual else "unmatched_initial_image"),
        "actual_ram_snapshot_used": args.ram is not None,
        "graphics_compatibility_established": False,
        "f3djfg_or_f3ddkr_enum_present": False,
        "opcode_gaps": {"0x04": "JFG G_VTX uses custom DMA encoding; RT64 F3D has a generic vertex handler",
                        "0x05": "JFG G_TRIN polygon command; no matching RT64 handler",
                        "0x07": "JFG G_DMADL DMA display-list command; no matching RT64 handler"},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/jfg.us.elf")
    parser.add_argument("--ram", type=Path, help="Actual 8 MiB big-endian RDRAM snapshot")
    parser.add_argument("--rt64-source", type=Path, default=ROOT / "build/port-graphics/rt64-source")
    parser.add_argument("--report", type=Path, help="Write a JSON report containing hashes and metadata only")
    args = parser.parse_args()
    result = run(args)
    output = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
