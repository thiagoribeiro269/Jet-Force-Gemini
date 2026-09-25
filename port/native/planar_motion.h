#pragma once
#include "animation.h"
#include <algorithm>

namespace jfg_native {

struct CharacterInput { double x = 0, z = 0; bool low = false; };
struct CharacterClips { uint32_t rest, moving, low; };
enum class CharacterState { Rest, Moving, Low };
inline const char *stateName(CharacterState state) {
    switch (state) {
        case CharacterState::Rest: return "rest";
        case CharacterState::Moving: return "moving";
        case CharacterState::Low: return "low";
    }
    throw std::runtime_error("Invalid native character state");
}

// Explicit PC prototype policy, not reconstructed JFG physics or control logic.
// Inputs are world-space axes. Local forward is -Z; Y stays on a flat plane.
// Bone root motion is visual only and never accumulated into world position.
class PlanarMotion {
    CharacterState state_ = CharacterState::Rest;
    double x_ = 0, z_ = 0, yaw_ = 0, vx_ = 0, vz_ = 0;
public:
    static constexpr double Speed = 60, DeadZone = 0.15, BlendSeconds = 0.25;
    static constexpr double TurnRate = 3.14159265358979323846, MaxStep = 0.25;
    PlanarMotion(double x = 0, double z = 0, double yaw = 0) : x_(x), z_(z), yaw_(yaw) {
        if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(yaw) || std::abs(x) > 1000000 || std::abs(z) > 1000000)
            throw std::runtime_error("Invalid native spawn transform");
        yaw_ = std::remainder(yaw_, 2 * TurnRate);
    }
    static void validateInput(const CharacterInput &input) {
        if (!std::isfinite(input.x) || !std::isfinite(input.z) || std::abs(input.x) > 1 || std::abs(input.z) > 1)
            throw std::runtime_error("Character axes must be finite and inside [-1,1]");
    }
    void command(const CharacterInput &input) {
        validateInput(input);
        const double magnitude = std::hypot(input.x, input.z);
        state_ = input.low ? CharacterState::Low : magnitude > DeadZone ? CharacterState::Moving : CharacterState::Rest;
        const double factor = state_ == CharacterState::Moving ? Speed / std::max(1.0, magnitude) : 0;
        vx_ = input.x * factor; vz_ = input.z * factor;
    }
    void advance(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0 || seconds > MaxStep)
            throw std::runtime_error("Invalid native character time step");
        const double nextX = x_ + vx_ * seconds, nextZ = z_ + vz_ * seconds;
        if (std::abs(nextX) > 1000000 || std::abs(nextZ) > 1000000)
            throw std::runtime_error("Native character left the supported coordinate range");
        double nextYaw = yaw_;
        if (state_ == CharacterState::Moving) {
            const double target = std::atan2(-vx_, -vz_);
            const double delta = std::remainder(target - yaw_, 2 * TurnRate);
            nextYaw = std::remainder(yaw_ + std::clamp(delta, -TurnRate * seconds, TurnRate * seconds), 2 * TurnRate);
        }
        x_ = nextX; z_ = nextZ; yaw_ = nextYaw;
    }
    CharacterState state() const { return state_; }
    double x() const { return x_; }
    double z() const { return z_; }
    double yaw() const { return yaw_; }
    double speed() const { return std::hypot(vx_, vz_); }
    Matrix world() const { return localMatrix({0, float(yaw_), 0}, {float(x_), 0, float(z_)}); }
};
}
