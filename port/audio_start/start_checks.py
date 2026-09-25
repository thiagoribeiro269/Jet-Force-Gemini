"""Check the original audio-start side effects at the next explicit boundary."""


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_audio_start(session):
    syms, profile = session.symbols, session.manifest[session.profile_key]
    queue, client = profile["thread_queue"], profile["thread_client"]
    require(session.word(queue) == profile["thread_address"] and session.word(queue + 8) == 0,
            "Audio receiver is not waiting on the empty queue")
    previous_client = syms["D_800FEC40_B1830"]
    require(session.read(client, 1) == b"\x01" and session.word(client + 4) == previous_client
            and session.word(client + 8) == queue, "Original audio scheduler client differs")
    require(session.read(previous_client, 1) == b"\x02" and session.word(previous_client + 4) == 0
            and session.word(previous_client + 8) == syms["D_800FEB80_B1770"],
            "Audio startup damaged the preexisting scheduler client")
    # osScAddClient loads/stores clientList at +0x2E0 in this US ELF.
    require(session.word(syms["sc"] + 0x2E0) == client, "Scheduler did not register the audio client")
    top = profile["thread_stack_top"]
    require(session.word(top - 0x14) == 0 and session.word(top - 0x18) == 0,
            "Original audio message slots were not cleared before waiting")
    require(session.word(top - 0x24) == 0x807FF000, "Audio thread return sentinel was not saved")

    table = session.word(syms["overlayTable"])
    require(session.word(table + 25 * 32) == 0 and session.lib.jfg_poc_address(b"amInit") == 0,
            "Bootstrap did not release the audio initialization overlay")
    require(session.word(table + 36 * 32) != 0, "Main bootstrap overlay was unexpectedly released")

    tune, ambient = session.word(syms["tuneSeqPlayer"]), session.word(syms["ambientSeqPlayer"])
    control_queue = syms["animCtrlQueue"]
    require(session.word(control_queue + 8) == 0 and session.word(control_queue + 16) == 1
            and session.word(control_queue + 20) == syms["animCtrlMesgBuf"], "Animation control queue differs")
    require(session.word(tune + 0x88) == control_queue and session.word(ambient + 0x88) == 0,
            "Sequence-player notification queues differ")
    require(session.read(syms["D_80105010_B1750"], 3) == b"\x01\x00\x00",
            "Original surround output configuration differs")
    # Bank events remain queued; mute-mode initialization additionally posts a
    # volume event to the ambient player. No player callback has run yet.
    counts = {}
    for name, player, count in (("tune", tune, 1), ("ambient", ambient, 2)):
        event_queue = player + 0x48
        require(int.from_bytes(session.read(event_queue + 0x10, 2), "big") == count,
                f"{name} startup event count differs")
        counts[name] = count
    return {"audio_thread": "blocked_on_empty_queue", "client_id": 1,
            "scheduler_client": f"0x{client:08X}", "receive_queue": f"0x{queue:08X}",
            "overlay25_released": True, "amInit_returned": True,
            "animation_queue_capacity": 1, "queued_sequence_events": counts,
            "surround_output_flags": [1, 0, 0], "audio_frames_processed": 0}
