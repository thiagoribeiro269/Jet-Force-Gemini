#pragma once
#include "region_scenario.h"

namespace jfg_native {

// Movement proof in Forest First. The controller script is expressed in
// original 1/60-second frames and raw N64 pad values (OSContPad), as a player
// would hold them. It is a reproducible test input, not a recording. The
// session runs joyRead on it; the camera is the original free camera, whose
// yaw the session feeds back as *controlcam.
struct MovementPhase { uint32_t from, to; int8_t stickX, stickY; uint16_t buttons; const char *name; };
inline const std::array<MovementPhase, 7> &movementScript() {
    static const std::array<MovementPhase, 7> script{{
        {0, 30, 0, 0, 0, "land from the original entry point"},
        {30, 330, 0, 70, 0, "run along the path and up the slope"},
        {330, 340, 0, 70, Pad::A, "running jump at the top"},
        {340, 460, 0, -70, 0, "turn around and run back"},
        {460, 480, 0, 0, 0, "release"},
        {480, 520, 0, 0, Pad::CLeft, "C-left: strafe left while the camera swings behind"},
        {520, 540, 0, 0, 0, "settle"},
    }};
    return script;
}
constexpr uint32_t MovementTicks = 540;

inline PadState movementPad(uint64_t tick) {
    PadState pad;
    for (const auto &phase : movementScript())
        if (tick >= phase.from && tick < phase.to) pad = {phase.buttons, phase.stickX, phase.stickY};
    return pad;
}

inline TickInput movementScene(const NativeRegion &region) {
    ActorInput input; input.pad = PadState{};
    SpawnSpec spawn{1, region.sourceSpawn[0], region.sourceSpawn[1], region.sourceSpawn[2], 0, input, region.playerScale, 0.0f};
    spawn.originalBody = true;
    spawn.originalYaw = 0; // The inspected setup point has zero angle fields.
    TickInput result; result.scene = SceneSpec{region.name, false, {spawn}, region.level}; return result;
}
}
