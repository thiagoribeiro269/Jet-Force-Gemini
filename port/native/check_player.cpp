#include "animation_player.h"
#include <iostream>
#include <functional>

using namespace jfg_native;
static void close(double a, double b) { if (std::abs(a - b) > 0.0001) throw std::runtime_error("Native player state differs"); }
static void same(const Keyframe &a, const Keyframe &b) {
    if (a.angles.size() != b.angles.size()) throw std::runtime_error("Pose sizes differ");
    for (unsigned axis = 0; axis < 3; ++axis) {
        close(a.root[axis], b.root[axis]);
        for (size_t i = 0; i < a.angles.size(); ++i) close(a.angles[i][axis], b.angles[i][axis]);
    }
}
int main() {
    try {
        Clip a, b; a.id = 11; a.sourceRate = 1; a.loop = true;
        a.keys = {{{0, 0, 0}, {{0, 0, 0}}}, {{2, 0, 0}, {{0, 0, 0}}}};
        b = a; b.id = 22; b.sourceRate = 2; b.keys[0].root[0] = 10; b.keys[1].root[0] = 14;
        const std::vector<Clip> clips{a, b};
        AnimationPlayer player(clips, 1, 11);
        player.advance(0.5); close(player.current().root[0], 1);
        if (player.select(11, 1)) throw std::runtime_error("Repeated ID restarted");
        close(player.frame(), 0.5);
        const auto before = player.current(); player.select(22, 1); same(before, player.current());
        player.advance(0.5); close(player.current().root[0], 7.5); close(player.weight(), 0.5);
        const auto interrupted = player.current(); player.select(11, 0.2); same(interrupted, player.current());
        player.advance(0.1); const auto unchanged = player.current(); const auto phase = player.frame();
        if (player.select(11, 0.8)) throw std::runtime_error("Repeated target reset transition");
        same(unchanged, player.current()); close(player.frame(), phase); close(player.weight(), 0.5);
        player.advance(0.1);
        if (player.transitioning()) throw std::runtime_error("Transition did not complete");
        same(player.current(), sampleClip(&a, 1, 0.2f));
        AnimationPlayer one(clips, 1, 11), split(clips, 1, 11);
        one.select(22, 0.3); split.select(22, 0.3); one.advance(0.3);
        for (unsigned i = 0; i < 9; ++i) split.advance(1.0 / 30);
        same(one.current(), split.current()); close(one.frame(), split.frame());
        if (one.transitioning() || split.transitioning()) throw std::runtime_error("Time-step partition changed completion");
        const auto saved = player.current(); const auto savedId = player.id(); const auto savedFrame = player.frame();
        unsigned rejected = 0;
        auto reject = [&](std::function<void()> f) { try { f(); } catch (const std::runtime_error &) { ++rejected; return; } throw std::runtime_error("Invalid player command accepted"); };
        reject([&] { player.select(999, 0.2); });
        reject([&] { player.select(22, -1); });
        reject([&] { player.select(22, std::numeric_limits<double>::quiet_NaN()); });
        reject([&] { player.advance(-1); });
        reject([&] { player.advance(std::numeric_limits<double>::infinity()); });
        reject([&] { const std::vector<Clip> duplicate{a, a}; AnimationPlayer invalid(duplicate, 1, 11); });
        same(saved, player.current()); close(player.frame(), savedFrame);
        if (player.id() != savedId) throw std::runtime_error("Failed request changed selection");
        a.keys[0].angles[0][2] = a.keys[1].angles[0][2] = 350 * Pi / 180;
        b.keys[0].angles[0][2] = b.keys[1].angles[0][2] = 10 * Pi / 180;
        const std::vector<Clip> wrapped{a, b}; AnimationPlayer angular(wrapped, 1, 11);
        angular.select(22, 1); angular.advance(0.5);
        close(std::cos(angular.current().angles[0][2]), 1);
        close(std::sin(angular.current().angles[0][2]), 0);
        b.loop = false; const std::vector<Clip> once{b}; AnimationPlayer clamped(once, 1, 22);
        clamped.advance(100); close(clamped.frame(), 1); close(clamped.current().root[0], 14);
        std::cout << "{\"status\":\"passed\",\"controller_cases\":8,\"rejected_cases\":" << rejected << "}\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
