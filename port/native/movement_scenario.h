#pragma once
#include "region_scenario.h"

namespace jfg_native {

// Movement proof in Forest First. The controller script is expressed in
// original 1/60-second frames and raw pad values, as a player would hold
// them. It is a reproducible test input, not a recording. The camera is the
// original free camera, whose yaw the session feeds back as *controlcam.
struct MovementPhase { uint32_t from, to; int32_t stickX, stickY; bool jumpHeld, cLeft; const char *name; };
inline const std::array<MovementPhase, 7> &movementScript() {
    static const std::array<MovementPhase, 7> script{{
        {0, 30, 0, 0, false, false, "land from the original entry point"},
        {30, 330, 0, 70, false, false, "run along the path and up the slope"},
        {330, 340, 0, 70, true, false, "running jump at the top"},
        {340, 460, 0, -70, false, false, "turn around and run back"},
        {460, 480, 0, 0, false, false, "release"},
        {480, 520, 0, 0, false, true, "rotate the camera with C-left"},
        {520, 540, 0, 0, false, false, "settle"},
    }};
    return script;
}
constexpr uint32_t MovementTicks = 540;

inline JunoControl movementControl(uint64_t tick) {
    JunoControl control;
    for (const auto &phase : movementScript()) {
        if (tick < phase.from || tick >= phase.to) continue;
        control.stickX = phase.stickX; control.stickY = phase.stickY; control.jumpHeld = phase.jumpHeld; control.cLeft = phase.cLeft;
        control.jumpPressed = phase.jumpHeld && tick == phase.from;
    }
    return control;
}

inline TickInput movementScene(const NativeRegion &region) {
    ActorInput input; input.control = JunoControl{};
    SpawnSpec spawn{1, region.sourceSpawn[0], region.sourceSpawn[1], region.sourceSpawn[2], 0, input, region.playerScale, 0.0f};
    spawn.originalBody = true;
    spawn.originalYaw = 0; // The inspected setup point has zero angle fields.
    TickInput result; result.scene = SceneSpec{region.name, false, {spawn}, region.level}; return result;
}
}
