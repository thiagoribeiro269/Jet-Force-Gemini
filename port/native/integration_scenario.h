#pragma once
#include "session.h"

namespace jfg_native {
// Integration fixture, not a level or control script extracted from the game.
inline ActorInput integrationIdle() {
    ActorInput input; input.selection.flag1FA = true; return input;
}
inline ActorInput integrationMove(double x, double z, float sourceMotion) {
    ActorInput input; input.movement = {x, z, false}; input.selection.component04 = sourceMotion; return input;
}
inline TickInput integrationInitial() {
    TickInput input;
    input.scene = SceneSpec{"native_integration_a", false,
        {{1, -120, 0, 0, 0, integrationMove(0.4, 0, 1)}, {2, 120, 0, 0, 0, integrationIdle()}}};
    return input;
}
inline TickInput integrationInput(uint64_t tick) {
    TickInput input;
    switch (tick) {
        case 60: input.actors.push_back({{1, 1}, integrationIdle()}); break;
        case 90: input.actors.push_back({{1, 2}, integrationMove(0, -0.4, 2)}); break;
        case 120: input.pause = true; break;
        case 150: input.actors.push_back({{1, 2}, integrationMove(-0.3, 0, 4)}); break; // Held while paused.
        case 180: input.pause = false; break;
        case 210: {
            auto low = integrationIdle(); low.movement.low = true; low.directMove = 14;
            input.actors.push_back({{1, 1}, low}); break;
        }
        case 240: input.scene = SceneSpec{"native_integration_b", false, {{1, 0, 0, 0, 0, integrationIdle()}}}; break;
        case 270: input.actors.push_back({{2, 1}, integrationMove(-0.3, 0, 4)}); break;
        case 300: input.spawn.push_back({2, 120, 0, 0, 0, integrationIdle()}); break;
        case 320: input.remove.push_back({2, 1}); break;
        case 340: input.actors.push_back({{2, 2}, integrationMove(-0.2, 0, 1)}); break;
        default: break;
    }
    return input;
}
// Presentation sample at i/fps, rounded upwards to a nanosecond so an exact
// simulation boundary is not accidentally sampled one tick early.
inline uint64_t presentationTime(uint64_t frame, uint64_t fps) {
    if (!fps || fps > 1000 || frame > 36000) throw std::runtime_error("Invalid presentation sample");
    return (frame * NativeSession::ClockScale + fps - 1) / fps;
}
}
