"""Independent checks of original audio-map pools, without playing sounds."""
from start_checks import check_audio_start, require


def check_audio_map(session):
    startup = check_audio_start(session)
    syms = session.symbols
    pool = session.word(syms["D_800F29F8_B1758"])
    free = session.word(syms["D_800F2A00_B1760"])
    active = session.word(syms["D_800F35F4"])
    ranges = ((pool, 0x5A0), (free, 0xA0), (active, 0xA0))
    for i, (address, size) in enumerate(ranges):
        require(0x80000000 <= address <= 0x80400000 - size and address % 16 == 0,
                "Audio map allocation outside original aligned heap")
        for other, other_size in ranges[:i]:
            require(address + size <= other or other + other_size <= address, "Audio map allocations overlap")
    require(session.word(syms["D_800F35F0"]) == session.word(syms["sfxIndex"]),
            "Audio map does not reference original sound settings")
    for index in range(40):
        require(session.word(free + index * 4) == pool + index * 0x24, "Audio-map free pointer differs")
        require(session.word(pool + index * 0x24 + 0x18) == 0, "Audio-map slot has a stale sound handle")
    require(session.read(syms["D_800F29FC_B175C"], 1) == bytes([39]), "Audio map free stack index differs")
    require(session.read(syms["D_800A0800_A1400"], 2) == bytes(2), "Audio map already has active points")
    require(session.read(syms["D_800F3604"], 1) == bytes(1), "Audio map ambient pause flag differs")
    first = syms["D_800F2A48_B17A8"]
    require(all(session.word(first + index * 0x10) == 0 for index in range(12)),
            "Audio map ambient handles were not reset")
    return {"startup": startup, "map": {"sound_slots": 40, "slot_bytes": 0x24,
            "allocations": [{"address": f"0x{address:08X}", "bytes": size} for address, size in ranges],
            "free_stack_index": 39, "active_points": 0, "cleared_ambient_handles": 12}}
