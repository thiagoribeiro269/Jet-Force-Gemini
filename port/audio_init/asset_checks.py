"""Independent ROM/RAM checks for amInit before amCreateAudioMgr.

Only the B1 root pointer and S1 entry layout used by the US audio assets are
parsed here. Deeper libaudio structures are outside this check's scope.
"""
from __future__ import annotations

import hashlib
import struct


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def _u16(data: bytes | bytearray, offset: int) -> int:
    _require(0 <= offset <= len(data) - 2, "Audio metadata exceeds its section")
    return struct.unpack_from(">H", data, offset)[0]


def _u32(data: bytes | bytearray, offset: int) -> int:
    _require(0 <= offset <= len(data) - 4, "Audio metadata exceeds its section")
    return struct.unpack_from(">I", data, offset)[0]


def _section(rom: bytes, lut: bytes, lut_end: int, index: int) -> tuple[bytes, int]:
    start, end = _u32(lut, 4 * (index + 1)), _u32(lut, 4 * (index + 2))
    _require(start <= end and lut_end + end <= len(rom), "Audio asset outside ROM")
    address = lut_end + start
    return rom[address:lut_end + end], address


def _check_bank(session, name: str, source: bytes, pointer: int) -> dict:
    _require(pointer != 0 and len(source) >= 8, f"{name} bank missing")
    _require(_u16(source, 0) == 0x4231, f"{name} bank is not B1")
    count = _u16(source, 2)
    _require(0 < count <= (len(source) - 4) // 4, f"{name} bank count exceeds section")
    _require(session.read(pointer, 4) == source[:4], f"{name} bank header differs")
    for i in range(count):
        root_offset = _u32(source, 4 + i * 4)
        _require(root_offset <= len(source) - 0xC, f"{name} root leaves bank")
        _require(session.word(pointer + 4 + i * 4) == pointer + root_offset,
                 f"{name} root pointer was not relocated")
        _require(session.read(pointer + root_offset, 2) == source[root_offset:root_offset + 2],
                 f"{name} root instrument count differs")
    return {"bytes": len(source), "bank_count": count,
            "rom_sha256": hashlib.sha256(source).hexdigest()}


def check_audio_assets(session, *, initial_heap_only=True) -> dict:
    """Check original US audio metadata against guest RAM at the manager stop."""
    profile = session.manifest.get("manager_profile") or session.manifest["audio_profile"]
    symbols, rom = session.symbols, session.rom
    _require(profile["asset_sections"] == {"offset_table": 0x33, "audio_data": 0x34},
             "Unexpected audio asset indices")
    lut_start, lut_end = symbols["__ASSETS_LUT_START"], symbols["__ASSETS_LUT_END"]
    _require(0 <= lut_start < lut_end <= len(rom) and (lut_end - lut_start) % 4 == 0,
             "Asset LUT outside ROM")
    lut = rom[lut_start:lut_end]
    _require(_u32(lut, 0) >= 0x34, "Audio asset indices absent from LUT")
    _require(session.read(session.word(symbols["gAssetsLookupTable"]), len(lut)) == lut,
             "RAM asset LUT differs from ROM")
    table, _ = _section(rom, lut, lut_end, 0x33)
    audio, audio_address = _section(rom, lut, lut_end, 0x34)
    _require(len(table) >= 0x24, "Audio section directory too short")
    (seq_bank_end, sfx_bank_start, sfx_bank_end, seq_file_start,
     seq_index_start, sfx_index_start, lengths_start, lengths_end, duplicate_end) = (
        _u32(table, i) for i in range(0, 0x24, 4))
    _require(0 < seq_bank_end <= len(audio), "Sequence bank exceeds audio asset")
    _require(sfx_bank_start < sfx_bank_end <= len(audio), "Sound bank exceeds audio asset")
    _require(seq_file_start < len(audio), "S1 file outside audio asset")
    _require(seq_index_start <= sfx_index_start <= lengths_start <= lengths_end <= len(audio),
             "Audio index/length bounds invalid")
    _require(duplicate_end == lengths_end, "Audio directory end differs")

    seq_index_bytes = sfx_index_start - seq_index_start
    sfx_index_bytes = lengths_start - sfx_index_start
    for name, start, end, size_name in (
        ("seqIndex", seq_index_start, sfx_index_start, "seqIndexSize"),
        ("sfxIndex", sfx_index_start, lengths_start, "sfxIndexSize"),
    ):
        _require(session.word(symbols[size_name]) == end - start, f"{name} size differs")
        _require(session.read(session.word(symbols[name]), end - start) == audio[start:end],
                 f"{name} RAM differs from ROM")
    _require(session.word(symbols["maxSequence"]) == seq_index_bytes // 3,
             "maxSequence differs from original division")
    _require(session.word(symbols["maxSound"]) == sfx_index_bytes // 10,
             "maxSound differs from original division")

    banks = {
        "sequence": _check_bank(session, "sequence", audio[:seq_bank_end],
                                session.word(symbols["seqBankPtr"])),
        "sound": _check_bank(session, "sound", audio[sfx_bank_start:sfx_bank_end],
                             session.word(symbols["sfxBankPtr"])),
    }

    _require(_u16(audio, seq_file_start) == 0x5331, "S1 sequence header missing")
    count = _u16(audio, seq_file_start + 2)
    seq_file_bytes = 4 + 8 * count
    _require(count > 0 and seq_file_start + seq_file_bytes <= len(audio),
             "S1 sequence count exceeds audio asset")
    expected_file = bytearray(audio[seq_file_start:seq_file_start + seq_file_bytes])
    expected_lengths = bytearray()
    maximum = 0
    for i in range(count):
        entry = 4 + 8 * i
        sequence_offset = _u32(expected_file, entry)
        raw_length = _u32(expected_file, entry + 4)
        _require(sequence_offset <= len(audio) - seq_file_start,
                 "S1 sequence offset outside audio asset")
        _require(raw_length <= len(audio) - seq_file_start - sequence_offset,
                 "S1 sequence length outside audio asset")
        struct.pack_into(">I", expected_file, entry, audio_address + seq_file_start + sequence_offset)
        rounded = raw_length + (raw_length & 1)
        expected_lengths += struct.pack(">I", rounded)
        maximum = max(maximum, rounded)
    _require(session.read(session.word(symbols["seqFile"]), seq_file_bytes) == expected_file,
             "S1 sequence file load or pointer relocation differs")
    _require(session.read(session.word(symbols["seqLen"]), len(expected_lengths)) == expected_lengths,
             "seqLen differs from original rounded lengths")

    heap = symbols["hp"]
    heap_base, heap_cur, heap_len, heap_count = (session.word(heap + i) for i in (0, 4, 8, 12))
    _require(heap_base == symbols["audioHeap"] and heap_len == 0x2F990,
             "libaudio heap base or capacity differs")
    # alHeapDBAlloc reserves one aligned 4-byte S1 header (16 bytes). The
    # full seqFile is subsequently allocated with mmAlloc, outside ALHeap.
    _require(heap_base + 16 <= heap_cur <= heap_base + heap_len and heap_cur % 16 == 0,
             "libaudio heap cursor is outside its aligned capacity")
    if initial_heap_only:
        _require(heap_cur == heap_base + 16 and heap_count == 0,
                 "libaudio heap cursor differs after S1 header allocation")
    _require(session.read(heap_base, 4) == audio[seq_file_start:seq_file_start + 4],
             "S1 header in libaudio heap differs from ROM")
    return {"asset_table_bytes": len(table), "audio_asset_bytes": len(audio),
            "banks": banks, "sequence_count": count, "max_sequence_bytes": maximum,
            "seq_index_bytes": seq_index_bytes, "sfx_index_bytes": sfx_index_bytes,
            "heap_used_bytes": heap_cur - heap_base}
