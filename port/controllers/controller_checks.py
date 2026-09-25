"""Check original controller state and one explicit SI completion."""
from map_checks import check_audio_map
from start_checks import require

OUTPUT = 0x807B2000


def check_controllers(session):
    mapping = check_audio_map(session)
    symbols = session.symbols
    queue = symbols["joyMessageQueue"]
    require(session.read(symbols["joyStatus"], 16) == bytes([0, 0, 0, 8]) * 4,
            "Absent-controller status differs from original cold-start layout")
    require(session.read(symbols["connected"], 4) == bytes(4)
            and session.read(symbols["enabled"], 4) == bytes(4), "Absent controllers were enabled")
    require(session.read(symbols["sPlayerID"], 4) == bytes(range(4)), "Controller mapping was not reset")
    require(session.word(symbols["numberOfJoypads"]) == 0 and session.word(symbols["joyfail"]) == 0xFFFFFFFF,
            "Original joyInit failure result was not preserved")
    require(session.word(queue + 8) == 0 and session.word(queue + 16) == 1, "Controller queue differs")
    before = session.controllers()
    require_success = lambda status: require(status == 0, f"Controller pump failed: {status}")
    require_success(session.lib.jfg_controllers_pump(session.native.memory))
    after = session.controllers()
    require(after["pending"] == 0 and after["si_delivered"] == 1 and after["si_dropped"] == 0,
            "SI read completion was not delivered once")
    require(session.word(queue + 8) == 1, "SI completion did not reach the game queue")
    require(session.call("osRecvMesg", queue, OUTPUT, 0)[2] == 0
            and session.word(OUTPUT) == session.word(symbols["joyMessage"]), "SI message content differs")
    require_success(session.lib.jfg_controllers_pump(session.native.memory))
    require(session.controllers() == after and session.word(queue + 8) == 0, "SI completion was duplicated")
    return {"audio_map": mapping, "controller_backend": "four absent ports; no physical device accessed",
            "connected": 0, "joyInit_result": -1, "before_pump": before, "after_pump": after}
