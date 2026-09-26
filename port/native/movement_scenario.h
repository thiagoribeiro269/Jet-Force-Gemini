#pragma once
#include "region_scenario.h"

namespace jfg_native {

// Movement proof in Forest First. The controller script is expressed in
// original 1/60-second frames and in camera-relative stick values, as a
// player would hold them. It is a reproducible test input, not a recording.
struct MovementPhase { uint32_t from, to; int32_t stickX, stickY; bool jumpHeld; const char *name; };
inline const std::array<MovementPhase, 6> &movementScript() {
    static const std::array<MovementPhase, 6> script{{
        {0, 30, 0, 0, false, "land from the original entry point"},
        {30, 330, 0, 70, false, "run south along the path and up the slope"},
        {330, 340, 0, 70, true, "running jump at the top"},
        {340, 460, 0, -70, false, "turn around and run back down"},
        {460, 470, 0, 0, false, "release"},
        {470, 540, 0, 0, false, "settle"},
    }};
    return script;
}
constexpr uint32_t MovementTicks = 540;

inline JunoControl movementControl(uint64_t tick, int16_t cameraYaw) {
    JunoControl control;
    control.cameraYaw = cameraYaw;
    for (const auto &phase : movementScript()) {
        if (tick < phase.from || tick >= phase.to) continue;
        control.stickX = phase.stickX; control.stickY = phase.stickY; control.jumpHeld = phase.jumpHeld;
        control.jumpPressed = phase.jumpHeld && tick == phase.from;
    }
    return control;
}

// Port camera policy, not the original camera system: keeps a fixed offset
// from Juno and turns behind him with an exponential lag only while the stick
// points forward, so running toward the camera or sideways stays readable.
// Its yaw is fed back as *controlcam so the stick stays camera-relative.
class FollowCamera {
    int16_t yaw_ = 0;
public:
    static constexpr float Distance = 170, Height = 75, LookHeight = 32;
    explicit FollowCamera(int16_t yaw = int16_t(0x8000)) : yaw_(yaw) {}
    int16_t yaw() const { return yaw_; }
    void follow(const JunoBody &body, int32_t stickX, int32_t stickY) {
        // Facing (-sin h, -cos h) equals camera forward (sin c, cos c) at c = h + 0x8000.
        const auto target = int16_t(body.heading11C + 0x8000);
        if (stickY > 0 && stickY > std::abs(stickX)) yaw_ = OriginalMath::dAngle(yaw_, target, 0.03f);
    }
    WorldCamera view(const JunoBody &body) const {
        const auto &math = body.data().math;
        const float fx = math.sinf(yaw_), fz = math.cosf(yaw_);
        WorldCamera camera;
        camera.eye = {body.position.x - fx * Distance, body.position.y + Height, body.position.z - fz * Distance};
        camera.target = {body.position.x + fx * 40, body.position.y + LookHeight, body.position.z + fz * 40};
        return camera;
    }
};

inline TickInput movementScene(const NativeRegion &region) {
    ActorInput input; input.control = JunoControl{};
    SpawnSpec spawn{1, region.sourceSpawn[0], region.sourceSpawn[1], region.sourceSpawn[2], 0, input, region.playerScale, 0.0f};
    spawn.originalBody = true;
    spawn.originalYaw = 0; // The inspected setup point has zero angle fields.
    TickInput result; result.scene = SceneSpec{region.name, false, {spawn}, region.level}; return result;
}
}
