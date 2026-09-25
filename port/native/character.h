#pragma once
#include "animation_player.h"
#include "planar_motion.h"

namespace jfg_native {
// Preserved diagnostic policy; the motion component is now independently usable.
class CharacterController {
    CharacterClips clips_;
    AnimationPlayer animation_;
    PlanarMotion motion_;
public:
    static constexpr double Speed = PlanarMotion::Speed, DeadZone = PlanarMotion::DeadZone, BlendSeconds = PlanarMotion::BlendSeconds;
    static constexpr double TurnRate = PlanarMotion::TurnRate, MaxStep = PlanarMotion::MaxStep;
    CharacterController(const std::vector<Clip> &clips, size_t channels, CharacterClips ids)
        : clips_(ids), animation_(clips, channels, ids.rest) {
        if (ids.rest == ids.moving || ids.rest == ids.low || ids.moving == ids.low)
            throw std::runtime_error("Character states require distinct clip IDs");
        for (auto id : {ids.rest, ids.moving, ids.low})
            if (std::none_of(clips.begin(), clips.end(), [id](const auto &c) { return c.id == id; }))
                throw std::runtime_error("Missing character animation");
    }
    static void validateInput(const CharacterInput &input) { PlanarMotion::validateInput(input); }
    bool command(const CharacterInput &input) {
        auto next = motion_; next.command(input);
        const auto state = next.state();
        const auto id = state == CharacterState::Low ? clips_.low : state == CharacterState::Moving ? clips_.moving : clips_.rest;
        const bool changed = animation_.select(id, BlendSeconds);
        motion_ = next; return changed;
    }
    void advance(double seconds) {
        auto next = motion_; next.advance(seconds);
        animation_.advance(seconds); motion_ = next;
    }
    CharacterState state() const { return motion_.state(); }
    double x() const { return motion_.x(); }
    double z() const { return motion_.z(); }
    double yaw() const { return motion_.yaw(); }
    double speed() const { return motion_.speed(); }
    const AnimationPlayer &animation() const { return animation_; }
    Matrix world() const { return motion_.world(); }
};
}
