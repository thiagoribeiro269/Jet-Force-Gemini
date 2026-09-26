#pragma once
#include "region_scenario.h"

namespace jfg_native {

// Movement proof in Forest First. The controller script is expressed in
// original 1/60-second frames and raw N64 pad values (OSContPad), as a player
// would hold them. It is a reproducible test input, not a recording. The
// session runs joyRead on it; the camera is the original free camera, whose
// yaw the session feeds back as *controlcam.
struct MovementPhase { uint32_t from, to; int8_t stickX, stickY; uint16_t buttons; const char *name; };
inline const std::array<MovementPhase, 29> &movementScript() {
    static const std::array<MovementPhase, 29> script{{
        {0, 30, 0, 0, 0, "land from the original entry point"},
        {30, 330, 0, 70, 0, "run along the path and up the slope"},
        {330, 340, 0, 70, Pad::A, "running jump at the top"},
        {340, 460, 0, -70, 0, "turn around and run back"},
        {460, 480, 0, 0, 0, "release"},
        {480, 520, 0, 0, Pad::CLeft, "C-left: strafe left while the camera swings behind"},
        {520, 540, 0, 0, 0, "settle"},
        {540, 541, 0, 0, Pad::B, "B: crouch down (state 1)"},
        {541, 600, 0, 0, 0, "crouched"},
        {600, 660, 0, 70, 0, "crouch-walk (state 2)"},
        {660, 661, 0, 70, Pad::CLeft, "C-left: roll while crouch-walking"},
        {661, 700, 0, 70, 0, "roll, then crouch-walk"},
        {700, 720, 0, 0, 0, "stop crouched"},
        {720, 730, 0, 0, Pad::A, "A: stand up"},
        {730, 770, 0, 0, 0, "standing"},
        {770, 820, 0, 70, 0, "run"},
        {820, 821, 0, 70, Pad::B, "B while running: slide"},
        {821, 870, 0, 0, 0, "slide, then crouched"},
        {870, 900, 0, 0, Pad::A, "A: stand up"},
        {900, 940, 80, 0, Pad::R, "R: aim (state 0xB); stick at the right edge turns Juno"},
        {940, 970, 0, 80, Pad::R, "aim up"},
        {970, 990, 0, 0, uint16_t(Pad::R | Pad::CRight), "strafe right while aiming"},
        {990, 1020, 0, 0, 0, "release R: back to walking, camera behind"},
        {1020, 1021, 0, 0, Pad::B, "B: crouch"},
        {1021, 1080, 0, 0, 0, "crouch down fully; the clip blend ends"},
        {1080, 1120, 80, 0, Pad::R, "R: crouched aim (state 5), turning"},
        {1120, 1121, 0, 0, uint16_t(Pad::R | Pad::A), "A: stand into the standing aim"},
        {1121, 1140, 0, 0, Pad::R, "standing aim"},
        {1140, 1160, 0, 0, 0, "release R"},
    }};
    return script;
}
constexpr uint32_t MovementTicks = 1160;

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
