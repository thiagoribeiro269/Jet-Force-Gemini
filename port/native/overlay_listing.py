#!/usr/bin/env python3
"""Rewrite overlay ASM listings with statically resolved relocations for m2c.

Static analysis only: the tool reads relocation records from the local US ROM
through tools/overlay_reloc.py and rewrites spimdisasm text so a decompiler
can see real call targets, data addresses and jump tables. It executes no CPU
code, maps no console memory and writes only to the ignored analysis folders.

Addresses inside an overlay are replaced by decodable analysis addresses:
0xA0000000 + (overlay << 20) + section base + offset, where the section base
is 0x00000 for text, 0x40000 for data and 0x80000 for bss. External data keeps
its real main-program address. `decode()` maps both back to names.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from overlay_reloc import OverlayRelocTool  # noqa: E402

ANALYSIS_BASE = 0xA0000000
SECTION_BASE = {"text": 0x00000, "data": 0x40000, "bss": 0x80000}
LINE = re.compile(r"^(\s*/\*\s*([0-9A-F]+)\s+([0-9A-F]+)\s+([0-9A-F]{8})\s*\*/\s*)(\S+)(\s*)(.*)$")


def main_symbols():
    table = {}
    for line in (ROOT / "ver/symbols/symbol_addrs.us.txt").read_text().splitlines():
        match = re.match(r"\s*(\w+)\s*=\s*0x([0-9A-Fa-f]+)\s*;", line)
        if match:
            table.setdefault(int(match.group(2), 16), match.group(1))
    return table


def sext16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


class Overlay:
    def __init__(self, tool, number):
        self.tool, self.number = tool, number
        self.header = tool.get_overlay_header(number)
        self.rom = tool.offsets["overlay_data_base"] + self.header.rom_offset
        self.text, self.data, self.bss = self.header.text_size, self.header.data_size, self.header.bss_size
        self.relocs = {}
        for secondary in (False, True):
            for entry in tool.get_relocation_entries(number, secondary=secondary):
                if entry.target_offset in self.relocs:
                    raise RuntimeError(f"Overlay {number}: duplicate relocation at 0x{entry.target_offset:X}")
                self.relocs[entry.target_offset] = entry
        self.functions = {}
        folder = ROOT / f"asm/nonmatchings/overlays/o{number}/overlay_{number}"
        for path in sorted(folder.glob("*.s")):
            text = path.read_text()
            name = re.search(r"^glabel (\S+)", text, re.M).group(1)
            first = LINE.search(next(line for line in text.splitlines() if LINE.match(line)))
            self.functions[int(first.group(3), 16) - (number << 20)] = name

    def word(self, offset):
        return struct.unpack_from(">I", self.tool.rom, self.rom + offset)[0]

    def section(self, local):
        if 0 <= local < self.text:
            return "text", local
        if local < self.text + self.data:
            return "data", local - self.text
        if local <= self.text + self.data + self.bss:
            return "bss", local - self.text - self.data
        raise RuntimeError(f"Overlay {self.number}: local offset 0x{local:X} outside sections")

    def analysis_address(self, local):
        section, offset = self.section(local)
        if offset >= 0x8000 and section != "text":
            raise RuntimeError("Section too large for the analysis address layout")
        return ANALYSIS_BASE + (self.number << 20) + SECTION_BASE[section] + offset

    def external_address(self, index):
        overlay, offset = self.tool.resolve_ort_entry(index)
        if overlay == 0:
            return self.tool.offsets["base_addr"] + offset
        if overlay in (0xFFD, 0xFFE):
            return self.tool.offsets["data_base"] + offset
        if overlay == 0xFFF:
            return self.tool.offsets["bss_base"] + offset
        return None

    def jump_tables(self):
        """Data offsets that start runs of LOCAL word relocations into text."""
        tables = {}
        words = sorted(o for o, e in self.relocs.items() if e.patch_type == 2 and e.reloc_type == 1 and e.symbol_index == 0)
        run = []
        for offset in words + [None]:
            if run and (offset is None or offset != run[-1] + 4):
                entries = [self.word(o) for o in run]
                if all(0 <= target < self.text and target % 4 == 0 for target in entries):
                    tables[run[0]] = entries
                run = []
            if offset is not None:
                run.append(offset)
        return tables


def rewrite(overlay, listing_path):
    lines = listing_path.read_text().splitlines()
    parsed = []
    for line in lines:
        match = LINE.match(line)
        parsed.append((line, match, int(match.group(3), 16) - (overlay.number << 20) if match else None))
    tables = overlay.jump_tables()
    table_names = {offset: f"jtbl_ovl{overlay.number}_{offset - overlay.text:04X}" for offset in tables}
    used_tables, labels = {}, {}
    # First pass: pair LO16 with its HI16 and detect jump-table loads.
    hi_for_lo = {}
    last_hi = {}
    for index, (line, match, offset) in enumerate(parsed):
        if not match or offset not in overlay.relocs:
            continue
        entry = overlay.relocs[offset]
        if entry.patch_type == 5:
            last_hi[(entry.reloc_type, entry.symbol_index)] = index
        elif entry.patch_type == 6:
            hi_for_lo[index] = last_hi.get((entry.reloc_type, entry.symbol_index))
    jtbl_hi = {}
    for index, hi_index in hi_for_lo.items():
        entry = overlay.relocs[parsed[index][2]]
        if entry.reloc_type != 1:
            continue
        hi_imm = int(parsed[hi_index][1].group(4), 16) & 0xFFFF if hi_index is not None else 0
        local = entry.symbol_index + (hi_imm << 16) + sext16(int(parsed[index][1].group(4), 16))
        if local in table_names:
            used_tables[local] = table_names[local]
            if hi_index is not None:
                jtbl_hi[hi_index] = table_names[local]
            jtbl_hi[index] = table_names[local]
            for target in tables[local]:
                labels[target] = f".Ljt_ovl{overlay.number}_{target:05X}"
    def base_of(entry, offset):
        if entry.reloc_type == 1:
            return overlay.analysis_address(entry.symbol_index)
        base = overlay.external_address(entry.symbol_index)
        if base is None:
            raise RuntimeError(f"Overlay {overlay.number}: data reference into another overlay at 0x{offset:X}")
        return base

    # Full address of every LO16 and the MIPS-rounded high half of every HI16.
    full, high = {}, {}
    for index, hi_index in hi_for_lo.items():
        entry = overlay.relocs[parsed[index][2]]
        hi_imm = int(parsed[hi_index][1].group(4), 16) & 0xFFFF if hi_index is not None else 0
        full[index] = base_of(entry, parsed[index][2]) + (hi_imm << 16) + sext16(int(parsed[index][1].group(4), 16))
        if hi_index is not None:
            rounded = ((full[index] + 0x8000) >> 16) & 0xFFFF
            if high.setdefault(hi_index, rounded) != rounded:
                raise RuntimeError(f"Overlay {overlay.number}: HI16 at listing line {hi_index} serves different pages")
    output = []
    for index, (line, match, offset) in enumerate(parsed):
        if match and offset in labels:
            output.append(f"{labels[offset]}:")
        if not match or offset not in overlay.relocs:
            output.append(line)
            continue
        entry = overlay.relocs[offset]
        prefix, word, mnemonic, space, operands = match.group(1), int(match.group(4), 16), match.group(5), match.group(6), match.group(7)
        if entry.patch_type == 4:
            if entry.reloc_type == 2:
                target = (word & 0x03FFFFFF) << 2
                name = overlay.functions.get(target, f"func_ovl{overlay.number}_{target:05X}")
            else:
                name = overlay.tool.get_symbol_name(entry.symbol_index)
            output.append(f"{prefix}{mnemonic}{space}{name}")
            continue
        if entry.patch_type not in (5, 6):
            output.append(line)
            continue
        if index in jtbl_hi:
            symbol = jtbl_hi[index]
            new = f"%hi({symbol})" if entry.patch_type == 5 else f"%lo({symbol})"
        elif entry.patch_type == 5:
            if index not in high:
                high[index] = ((base_of(entry, offset) + ((word & 0xFFFF) << 16) + 0x8000) >> 16) & 0xFFFF
            new = f"0x{high[index]:X}"
        else:
            new = f"0x{full[index] & 0xFFFF:X}"
        if entry.patch_type == 5:
            operands = re.sub(r",\s*(%hi\([^)]*\)|\(0x[0-9A-Fa-f]+ >> 16\)|-?0x[0-9A-Fa-f]+|-?\d+)\s*$", f", {new}", operands)
        else:
            operands, count = re.subn(r"(%lo\([^)]*\)|\(0x[0-9A-Fa-f]+ & 0xFFFF\)|-?0x[0-9A-Fa-f]+|-?\d+)(\(\$\w+\))\s*$", f"{new}\\2", operands)
            if not count:
                operands = re.sub(r",\s*(%lo\([^)]*\)|\(0x[0-9A-Fa-f]+ & 0xFFFF\)|-?0x[0-9A-Fa-f]+|-?\d+)\s*$", f", {new}", operands)
        output.append(f"{prefix}{mnemonic}{space}{operands}")
    if used_tables:
        output.append(".section .rodata")
        for offset, name in sorted(used_tables.items()):
            output.append(f"glabel {name}")
            for target in tables[offset]:
                output.append(f".word {labels[target]}")
    return "\n".join(output) + "\n"


def decode(text):
    """Replace analysis/main addresses in decompiler output with readable names."""
    symbols = main_symbols()

    def label(address):
        if address in symbols:
            return symbols[address]
        if address & 0xF0000000 == ANALYSIS_BASE:
            overlay, rest = (address >> 20) & 0xFF, address & 0xFFFFF
            section = {0x00000: "text", 0x40000: "data", 0x80000: "bss"}[rest & 0xC0000]
            return f"ovl{overlay}_{section}_{rest & 0x3FFFF:04X}"
        return None

    def deref(match):
        name = label(int(match.group(2), 16))
        return f"{name}/*{match.group(1).strip()}*/" if name else match.group(0)

    def bare(match):
        name = label(int(match.group(1), 16))
        return f"&{name}" if name else match.group(0)

    text = re.sub(r"\*\(([\w\s\*]+)\)0x([89A][0-9A-F]{7})\b", deref, text)
    text = re.sub(r"\b0x([89A][0-9A-F]{7})\b", bare, text)
    return re.sub(r"\bD_([0-9A-F]{8})\b", lambda m: label(int(m.group(1), 16)) or m.group(0), text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("overlay", type=int)
    parser.add_argument("function")
    parser.add_argument("--rom", type=Path, default=ROOT / "baseroms/baserom.us.z64")
    parser.add_argument("--out", type=Path, default=ROOT / "m2cfiles")
    args = parser.parse_args()
    out = args.out.resolve()
    if not (out.is_relative_to(ROOT / "m2cfiles") or out.is_relative_to(ROOT / "build")):
        raise SystemExit("Analysis listings stay under ignored m2cfiles/ or build/")
    overlay = Overlay(OverlayRelocTool(str(args.rom)), args.overlay)
    folder = ROOT / f"asm/nonmatchings/overlays/o{args.overlay}/overlay_{args.overlay}"
    matches = list(folder.glob(f"{args.function}.s")) or list(folder.glob(f"*{args.function}*.s"))
    if len(matches) != 1:
        raise SystemExit(f"Expected one listing for {args.function}, found {len(matches)}")
    out.mkdir(parents=True, exist_ok=True)
    target = out / (matches[0].stem + ".reloc.s")
    target.write_text(rewrite(overlay, matches[0]))
    print(target)


if __name__ == "__main__":
    main()
