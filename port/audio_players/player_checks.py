"""Inspect the original sequence and sound players immediately before amGo.

The capacities and offsets below come from the US n_alCSPNew, gsSndpNew,
n_alEvtqNew, and overlay 25 amInit assembly.  The loader starts the audio
heap as zero-filled BSS; these checks describe that startup path.
"""


def _require(condition, message):
    if not condition:
        raise AssertionError(message)


def _half(session, address):
    return int.from_bytes(session.read(address, 2), "big")


def _byte(session, address):
    return session.read(address, 1)[0]


def _event_queue(session, address, capacity, queued, label, event_type=None, payload=None):
    # N_ALEventListItem is 0x1c bytes; the N64 queue has two ALLink
    # sentinels followed by three 16-bit counters at +0x10/+0x12/+0x14.
    _require(_half(session, address + 0x10) == queued, f"{label}: wrong queued count")
    _require(_half(session, address + 0x12) == 0, f"{label}: event overflow counter is nonzero")
    _require(_half(session, address + 0x14) == 1, f"{label}: wrong high-water mark")

    def walk(sentinel, count, name):
        nodes = []
        previous = sentinel
        current = session.word(sentinel)
        seen = set()
        while current:
            _require(current not in seen and len(nodes) < count,
                     f"{label}: {name} cycle or count overflow")
            _require(session.word(current + 4) == previous,
                     f"{label}: {name} has broken previous link")
            seen.add(current)
            nodes.append(current)
            previous, current = current, session.word(current)
        _require(len(nodes) == count, f"{label}: {len(nodes)} {name}, expected {count}")
        return nodes

    free = walk(address, capacity - queued, "free events")
    active = walk(address + 8, queued, "queued events")
    nodes = free + active
    _require(len(set(nodes)) == capacity, f"{label}: event node appears in both lists")
    # n_alEvtqNew creates one contiguous pool. Posting moves a node into the
    # active list; the sound constructor then consumes its first event.
    low = min(nodes)
    allowed = {low + i * 0x1C for i in range(capacity)}
    _require(set(nodes) == allowed, f"{label}: events are not one contiguous pool")
    if queued:
        _require(queued == 1 and session.word(active[0] + 8) == 0,
                 f"{label}: unexpected queued event delta")
        _require(_half(session, active[0] + 12) == event_type,
                 f"{label}: wrong queued event type")
        _require(session.word(active[0] + 16) == payload,
                 f"{label}: wrong queued event payload")
    return len(free)


def _sequence(session, pointer, voices, events, callback, synth, bank, label):
    _require(pointer != 0, f"{label}: null player")
    _require(session.word(pointer + 4) == pointer, f"{label}: clientData differs from player")
    _require(session.word(pointer + 8) == callback, f"{label}: wrong callback")
    _require(session.word(pointer + 0x10) == session.word(synth + 0x20),
             f"{label}: wrong samplesLeft at registration")
    _require(session.word(pointer + 0x14) == synth, f"{label}: wrong synth pointer")
    for offset in (0x18, 0x1C, 0x20, 0x28, 0x2C, 0x68, 0x6C, 0x88):
        _require(session.word(pointer + offset) == 0, f"{label}: expected zero at +0x{offset:X}")
    _require(session.word(pointer + 0x24) == 0x1E8, f"{label}: wrong microseconds per tick")
    _require(_half(session, pointer + 0x30) == 0xFFFF, f"{label}: wrong channel mask")
    _require(_half(session, pointer + 0x32) == 0x7FFF, f"{label}: wrong volume")
    _require(_byte(session, pointer + 0x34) == 16, f"{label}: wrong channel count")
    _require(_half(session, pointer + 0x38) == 9, f"{label}: wrong initial event type")
    _require(session.word(pointer + 0x60) == 0x3E80, f"{label}: wrong frame time")
    _require(session.word(pointer + 0x64) != 0, f"{label}: missing channel states")
    _require(session.word(pointer + 0x70) != 0, f"{label}: missing free voices")
    _require(session.word(pointer + 0x80) == 0 and session.word(pointer + 0x84) == 0x3F800000,
             f"{label}: wrong initial FX mix")
    _require(_byte(session, pointer + 0x8C) == voices and _byte(session, pointer + 0x8D) == 0,
             f"{label}: wrong voice limit/count")
    head = session.word(pointer + 0x70)
    base = head - (voices - 1) * 0x40
    allowed = {base + i * 0x40 for i in range(voices)}
    seen = set()
    current = head
    while current:
        _require(current in allowed and current not in seen, f"{label}: invalid free voice 0x{current:08X}")
        seen.add(current)
        current = session.word(current)
    _require(len(seen) == voices, f"{label}: {len(seen)} free voices, expected {voices}")
    free_events = _event_queue(session, pointer + 0x48, events, 1, label, 0xE, bank)
    return {"address": f"0x{pointer:08X}", "free_voices": len(seen),
            "free_events": free_events, "queued_events": 1, "next_delta_us": 0,
            "initial_event_type": 9, "queued_event_type": 0xE,
            "channels": 16, "volume": 0x7FFF}


def check_players(session):
    """Validate the original two sequence players and one sound player."""
    symbols = session.symbols
    profile = session.manifest["players_profile"]
    callbacks = profile["callbacks"]
    synth = session.word(symbols["n_syn"])
    tune = session.word(symbols["tuneSeqPlayer"])
    ambient = session.word(symbols["ambientSeqPlayer"])
    sound = session.word(symbols["gSoundPlayerPtr"])
    _require(synth and len({tune, ambient, sound}) == 3, "Player pointers are null or aliased")

    # amInit creates 32/150, then 16/50, then 32/200.  Each player is
    # prepended to n_syn->head by the n_alSynAdd*Player routines.
    _require(session.word(synth) == sound and session.word(sound) == ambient
             and session.word(ambient) == tune and session.word(tune) == 0,
             "Synth player list has wrong order or links")
    seq_callback = callbacks["func_80086C80"]
    bank = session.word(session.word(symbols["seqBankPtr"]) + 4)
    _require(bank != 0, "Missing sequence bank for initial queued event")
    sequences = {
        "tune": _sequence(session, tune, 32, 150, seq_callback, synth, bank, "tune"),
        "ambient": _sequence(session, ambient, 16, 50, seq_callback, synth, bank, "ambient"),
    }

    _require(session.word(sound + 4) == sound, "Sound clientData differs from player")
    _require(session.word(sound + 8) == callbacks["func_80084848"], "Wrong sound callback")
    _require(session.word(sound + 0x10) == session.word(synth + 0x20),
             "Wrong sound samplesLeft at registration")
    _require(session.word(sound + 0x40) == 0, "Sound target is not clear")
    _require(session.word(sound + 0x48) == 16 and session.word(sound + 0x58) == 16,
             "Sound channel limits differ from amInit config")
    _require(session.word(sound + 0x4C) == 0x3E80 and session.word(sound + 0x50) == 0x3E80,
             "Sound frame time/next delta differs from initial callback")
    _require(session.word(sound + 0x54) == 0, "Sound time advanced before amGo")
    _require(_half(session, sound + 0x2C) == 0x20, "Wrong initial sound event type")

    state_base = session.word(sound + 0x44)
    _require(state_base != 0 and session.word(symbols["gSoundStateLists"] + 8) == state_base,
             "Sound-state free head differs from allocated pool")
    _require(session.word(symbols["gSoundStateLists"]) == 0
             and session.word(symbols["gSoundStateLists"] + 4) == 0,
             "Sound states were allocated before amGo")
    state_allowed = {state_base + i * 0x48 for i in range(32)}
    # The first state relies on the loader's zero-filled BSS audio heap;
    # gsSndpNew links subsequent states with alLink.
    seen = set()
    current = state_base
    previous = None
    while current:
        _require(current in state_allowed and current not in seen, "Sound-state free list is invalid")
        if previous is not None:
            _require(session.word(current + 4) == previous,
                     "Sound-state free list has a broken previous link")
        seen.add(current)
        previous = current
        current = session.word(current)
    _require(seen == state_allowed, f"Sound-state free list has {len(seen)} of 32 entries")

    free_events = _event_queue(session, sound + 0x14, 200, 0, "sound")
    volume_pointer = session.word(symbols["gSoundGroupVolume"])
    _require(volume_pointer != 0, "Missing sound-group volumes")
    _require(all(_half(session, volume_pointer + i * 2) == 0x7FFF for i in range(5)),
             "Wrong initial sound-group volume")
    return {"sequence_players": sequences,
            "sound_player": {"address": f"0x{sound:08X}", "free_states": len(seen),
                             "free_events": free_events, "queued_events": 0,
                             "next_delta_us": 0x3E80, "initial_event_type": 0x20,
                             "groups": 5, "group_volume": 0x7FFF}}
