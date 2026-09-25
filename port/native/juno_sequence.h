#pragma once
#include "juno_selection.h"
#include <sstream>

namespace jfg_native {
// Diagnostic encoding only; not the original controller or player bit layout.
inline JunoSelectionState junoInput(float a, float b, uint32_t flags, uint32_t idle) {
    if (flags > 127) throw std::runtime_error("Unknown native Juno input flags");
    return {a, b, bool(flags & 1), bool(flags & 2), bool(flags & 4), bool(flags & 8),
            bool(flags & 16), bool(flags & 32), bool(flags & 64), idle};
}
struct JunoEvent { uint32_t frame; JunoSelectionState state; double startFraction, blendSeconds; };
struct JunoSequence { uint32_t frames = 0; std::vector<JunoEvent> events; };
inline JunoSequence readJunoSequence(std::istream &input, const JunoSelectionData &data, const std::vector<Clip> &clips) {
    JunoSequence result;
    std::string line;
    size_t bytes = 0;
    // Preflight all commands before rendering or mutating the real controller.
    if (clips.empty() || clips.front().keys.empty()) throw std::runtime_error("Empty Juno animation collection");
    JunoAnimationController validate(data, clips, clips.front().keys.front().angles.size());
    while (std::getline(input, line)) {
        bytes += line.size() + 1;
        if (bytes > 65536) throw std::runtime_error("Juno sequence too large");
        line = line.substr(0, line.find('#'));
        std::istringstream row(line); std::string first, extra;
        if (!(row >> first)) continue;
        if (!result.frames) {
            uint64_t count;
            if (first != "frames" || !(row >> count) || row >> extra || count < 2 || count > 180)
                throw std::runtime_error("Invalid Juno sequence frame budget");
            result.frames = uint32_t(count); continue;
        }
        std::istringstream values(line);
        uint64_t frame, flags, idle; float a, b; double fraction, blend;
        if (!(values >> frame >> a >> b >> flags >> idle >> fraction >> blend) || values >> extra ||
            frame >= result.frames || flags > 127 || idle < 16 || idle > 19 || result.events.size() >= 64 ||
            (!result.events.empty() && frame <= result.events.back().frame))
            throw std::runtime_error("Invalid or unordered Juno selection event");
        const auto state = junoInput(a, b, uint32_t(flags), uint32_t(idle));
        validate.motion(state, fraction, blend);
        result.events.push_back({uint32_t(frame), state, fraction, blend});
    }
    if (input.bad() || result.events.empty() || result.events.front().frame != 0)
        throw std::runtime_error("Incomplete Juno selection sequence");
    return result;
}
}
