#pragma once
#include "animation_player.h"
#include <fstream>
#include <cstring>

namespace jfg_native {

// Fields are named by original player-state offsets until their broader
// meaning is established. Values at +04/+10 are not normalized stick axes.
struct JunoSelectionState {
    float component04 = 0, component10 = 0;
    bool flag1FA = false, flag1F9 = false, flag1F4 = false, flag198 = false;
    bool walkingBack569 = false, heldObject5CC = false, gunWeight = false;
    uint32_t idleDraw = 16; // Result supplied by the caller of mathRnd(16,19).
};
struct JunoSelectionRow {
    std::array<uint8_t, 3> destinations{};
    std::array<uint8_t, 2> transitionProfiles{};
    uint32_t clip = 0;
};
struct JunoSelectionData { float stationaryThreshold = 0; std::vector<JunoSelectionRow> rows; };
struct JunoSelectedMove { uint32_t requested, local, clip, transitionProfile; };

class JunoSelector {
    JunoSelectionData data_;
public:
    explicit JunoSelector(JunoSelectionData data) : data_(std::move(data)) {
        if (data_.rows.empty() || data_.rows.size() > 64 || !std::isfinite(data_.stationaryThreshold) || data_.stationaryThreshold <= 0)
            throw std::runtime_error("Invalid Juno selection table");
        for (size_t i = 0; i < data_.rows.size(); ++i) {
            const auto &row = data_.rows[i];
            for (auto index : row.destinations) if (index >= data_.rows.size()) throw std::runtime_error("Juno remap outside table");
            for (auto profile : row.transitionProfiles) if (profile >= 7) throw std::runtime_error("Unknown Juno transition profile");
            for (size_t j = 0; j < i; ++j) if (data_.rows[j].clip == row.clip) throw std::runtime_error("Duplicate Juno clip ID");
        }
    }
    // Static source: overlay 16 +0x4E08..0x4F78. Finite host inputs only.
    uint32_t choose(const JunoSelectionState &state) const {
        if (!std::isfinite(state.component04) || !std::isfinite(state.component10))
            throw std::runtime_error("Nonfinite Juno motion component");
        const float a = std::abs(state.component04), b = std::abs(state.component10);
        const float speed = std::max(a, b);
        if (speed < data_.stationaryThreshold) {
            if (state.flag1FA) return 16;
            if (state.flag1F9 || state.flag1F4 || state.flag198) return 18;
            if (state.idleDraw < 16 || state.idleDraw > 19) throw std::runtime_error("Juno idle draw outside original range");
            return state.idleDraw;
        }
        if (a < b) return state.component10 < 0 ? 9 : 10;
        if (state.walkingBack569) return 3;
        if (speed > 3.5f) return 2;
        if (speed > 1.75f) return 1;
        return 0;
    }
    // Static source: overlay 16 +0x4F78..0x5120. Profile lookup must use
    // the resulting row, not the requested row. A held object wins first.
    JunoSelectedMove resolve(uint32_t requested, const JunoSelectionState &state) const {
        if (requested >= data_.rows.size()) throw std::runtime_error("Juno move outside original table");
        unsigned column = 0, profileColumn = 0;
        if (state.heldObject5CC) column = 2;
        else if (state.gunWeight) {
            column = state.flag1F4 ? 1 : 2;
            profileColumn = state.flag1F4 ? 1 : 0;
        }
        const auto selected = data_.rows[requested].destinations[column];
        const auto &destination = data_.rows[selected];
        return {requested, selected, destination.clip, destination.transitionProfiles[profileColumn]};
    }
    JunoSelectedMove fromMotion(const JunoSelectionState &state) const { return resolve(choose(state), state); }
    // A move already remapped (object +0x3B): its own row, clip and profile.
    JunoSelectedMove local(uint32_t move) const {
        if (move >= data_.rows.size()) throw std::runtime_error("Juno move outside original table");
        const auto &row = data_.rows[move];
        return {move, move, row.clip, row.transitionProfiles[0]};
    }
};

inline JunoSelectionData readJunoSelection(const char *path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != 16 + 52 * 8) throw std::runtime_error("Invalid private Juno table size");
    std::array<uint8_t, 16 + 52 * 8> bytes{};
    input.seekg(0); input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
    auto u32 = [&](size_t at) { return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 |
                                      uint32_t(bytes[at + 2]) << 16 | uint32_t(bytes[at + 3]) << 24; };
    if (!input || std::memcmp(bytes.data(), "JFGSEL1\0", 8) || u32(8) != 52)
        throw std::runtime_error("Wrong native Juno selection format");
    JunoSelectionData result;
    const auto thresholdBits = u32(12);
    std::memcpy(&result.stationaryThreshold, &thresholdBits, 4);
    if (result.stationaryThreshold != 0.1f) throw std::runtime_error("Unsupported original Juno threshold");
    for (size_t offset = 16; offset < bytes.size(); offset += 8) {
        if (bytes[offset + 5]) throw std::runtime_error("Nonzero Juno table padding");
        result.rows.push_back({{bytes[offset], bytes[offset + 1], bytes[offset + 2]},
                               {bytes[offset + 3], bytes[offset + 4]},
                               uint32_t(bytes[offset + 6]) | uint32_t(bytes[offset + 7]) << 8});
    }
    JunoSelector validate(result);
    return result;
}

// Native bridge for the two recovered decisions. The original profile index
// is propagated, but controlSetTransition itself is not implemented here.
// Blend seconds remain a separate native parameter from original start phase.
class JunoAnimationController {
    JunoSelector selector_;
    JunoSelectedMove selected_;
    AnimationPlayer animation_;
    uint32_t changes_ = 0;
public:
    JunoAnimationController(const JunoSelectionData &data, const std::vector<Clip> &clips, size_t channels, uint32_t initial = 16)
        : selector_(data), selected_(selector_.resolve(initial, {})), animation_(clips, channels, selected_.clip) {}
    bool request(uint32_t index, const JunoSelectionState &state, double startFraction, double blendSeconds) {
        const auto result = selector_.resolve(index, state);
        const bool changed = animation_.selectAt(result.clip, blendSeconds, startFraction);
        selected_ = result;
        changes_ += changed;
        return changed;
    }
    bool motion(const JunoSelectionState &state, double startFraction, double blendSeconds) {
        return request(selector_.choose(state), state, startFraction, blendSeconds);
    }
    // Follow the original move machine: switch clips only on a move change,
    // then place the clip at the original normalized position.
    bool follow(const JunoSelectedMove &move, double fraction, double blendSeconds, double seconds) {
        const bool changed = animation_.selectAt(move.clip, blendSeconds, fraction);
        animation_.place(fraction, changed ? 0.0 : seconds);
        selected_ = move;
        changes_ += changed;
        return changed;
    }
    const JunoSelector &selector() const { return selector_; }
    void advance(double seconds) { animation_.advance(seconds); }
    const JunoSelectedMove &selection() const { return selected_; }
    const AnimationPlayer &animation() const { return animation_; }
    uint32_t changes() const { return changes_; }
};
}
