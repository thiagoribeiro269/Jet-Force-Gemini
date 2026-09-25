#pragma once
#include "character.h"
#include <sstream>
#include <string>

namespace jfg_native {
struct CharacterEvent { uint32_t frame; CharacterInput input; };
struct CharacterSequence { uint32_t frames = 0; std::vector<CharacterEvent> events; };
inline CharacterSequence readCharacterSequence(std::istream &input) {
    CharacterSequence result;
    std::string line;
    size_t bytes = 0;
    while (std::getline(input, line)) {
        bytes += line.size() + 1;
        if (bytes > 65536) throw std::runtime_error("Character command file too large");
        line = line.substr(0, line.find('#'));
        std::istringstream row(line); std::string first, extra;
        if (!(row >> first)) continue;
        if (!result.frames) {
            uint64_t count;
            if (first != "frames" || !(row >> count) || row >> extra || count < 2 || count > 180)
                throw std::runtime_error("Invalid character frame budget");
            result.frames = uint32_t(count); continue;
        }
        std::istringstream values(line);
        uint64_t frame; int low; CharacterInput axes;
        if (!(values >> frame >> axes.x >> axes.z >> low) || values >> extra || frame >= result.frames ||
            (low != 0 && low != 1) || result.events.size() >= 64 ||
            (!result.events.empty() && frame <= result.events.back().frame))
            throw std::runtime_error("Invalid or unordered character command");
        axes.low = low != 0;
        CharacterController::validateInput(axes);
        result.events.push_back({uint32_t(frame), axes});
    }
    if (input.bad() || !result.frames || result.events.empty() || result.events.front().frame != 0)
        throw std::runtime_error("Incomplete native character sequence");
    return result;
}
}
