#pragma once
#include "animation.h"
#include <algorithm>
#include <limits>

namespace jfg_native {

// The immutable clip collection must outlive this player. This controls PC
// animation state, not original game locomotion or a console input device.
class AnimationPlayer {
    const std::vector<Clip> &clips_;
    size_t channels_, selected_;
    double frame_ = 0, duration_ = 0, elapsed_ = 0;
    Keyframe from_;

    size_t find(uint32_t id) const {
        for (size_t i = 0; i < clips_.size(); ++i) if (clips_[i].id == id) return i;
        throw std::runtime_error("Unknown native animation ID");
    }
public:
    AnimationPlayer(const std::vector<Clip> &clips, size_t channels, uint32_t initial)
        : clips_(clips), channels_(channels), selected_(0) {
        if (clips.empty() || channels == 0 || channels > 64) throw std::runtime_error("Invalid animation collection");
        for (size_t i = 0; i < clips.size(); ++i) {
            const auto &clip = clips[i];
            if (clip.keys.empty() || !std::isfinite(clip.sourceRate) || clip.sourceRate <= 0)
                throw std::runtime_error("Invalid clip clock or keys");
            for (size_t j = 0; j < i; ++j) if (clips[j].id == clip.id) throw std::runtime_error("Duplicate animation ID");
            for (const auto &key : clip.keys) {
                if (key.angles.size() != channels) throw std::runtime_error("Incompatible clip skeleton");
                for (float x : key.root) if (!std::isfinite(x)) throw std::runtime_error("Invalid root key");
                for (const auto &rotation : key.angles) for (float x : rotation)
                    if (!std::isfinite(x)) throw std::runtime_error("Invalid rotation key");
            }
        }
        selected_ = find(initial);
    }
    uint32_t id() const { return clips_[selected_].id; }
    double frame() const { return frame_; }
    bool transitioning() const { return duration_ > 0 && elapsed_ < duration_; }
    float weight() const {
        if (!transitioning()) return 1;
        const double t = elapsed_ / duration_;
        return float(t * t * (3 - 2 * t));
    }
    Keyframe current() const {
        auto target = sampleClip(&clips_[selected_], channels_, float(frame_));
        return transitioning() ? mixPoses(from_, target, weight()) : target;
    }
    bool select(uint32_t id, double seconds) {
        // Validate before changing anything: failure preserves the old state.
        if (!std::isfinite(seconds) || seconds < 0 || seconds > 10) throw std::runtime_error("Invalid transition duration");
        const size_t next = find(id);
        if (next == selected_) return false; // Holding a command never restarts playback.
        auto visible = current();
        selected_ = next; frame_ = 0; elapsed_ = 0; duration_ = seconds;
        from_ = std::move(visible);
        return true;
    }
    void advance(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0) throw std::runtime_error("Invalid animation time step");
        const auto &clip = clips_[selected_];
        const double next = frame_ + seconds * clip.sourceRate;
        if (!std::isfinite(next)) throw std::runtime_error("Animation clock overflow");
        frame_ = clip.loop ? std::fmod(next, double(clip.keys.size())) : std::min(next, double(clip.keys.size() - 1));
        if (transitioning()) {
            elapsed_ = std::min(duration_, elapsed_ + seconds);
            // Finish a mathematically complete interval despite accumulated
            // double rounding from e.g. nine 1/30-second steps.
            const double tolerance = 64 * std::numeric_limits<double>::epsilon() * std::max(1.0, duration_);
            if (duration_ - elapsed_ <= tolerance) elapsed_ = duration_;
        }
    }
};
}
