#include "character_sequence.h"
#include <functional>
#include <iostream>
#include <limits>

using namespace jfg_native;
static void check(bool condition, const char *why) { if (!condition) throw std::runtime_error(why); }
static void near(double a, double b) { check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 0.0001, "Character value differs"); }
static void samePose(const Keyframe &a, const Keyframe &b) {
    check(a.angles.size() == b.angles.size(), "Pose sizes differ");
    for (unsigned axis = 0; axis < 3; ++axis) {
        near(a.root[axis], b.root[axis]);
        for (size_t bone = 0; bone < a.angles.size(); ++bone) near(a.angles[bone][axis], b.angles[bone][axis]);
    }
}
int main() {
    try {
        Clip rest, moving, low;
        rest.id = 11; rest.sourceRate = 15;
        rest.keys = {{{4, 0, 0}, {{0, 0, 0}}}};
        moving = rest; moving.id = 22; moving.loop = true;
        moving.keys.push_back({{6, 0, 0}, {{0, 0, Pi / 4}}});
        low = rest; low.id = 33; low.keys[0].root = {1, -5, 2};
        const std::vector<Clip> clips{rest, moving, low};
        const CharacterClips ids{11, 22, 33};
        unsigned cases = 0, rejected = 0;
        CharacterController character(clips, 1, ids);
        check(character.state() == CharacterState::Rest && character.animation().id() == 11, "Wrong initial state");
        character.command({0.08, -0.06, false}); character.advance(0.2);
        near(character.x(), 0); near(character.z(), 0); near(character.yaw(), 0); near(character.speed(), 0);
        ++cases;

        const auto initial = character.animation().current();
        check(character.command({1, 0, false}), "Moving did not select a clip");
        samePose(initial, character.animation().current()); near(character.x(), 0); near(character.yaw(), 0);
        character.advance(0.2); near(character.x(), 12); near(character.yaw(), -0.2 * CharacterController::TurnRate);
        const double clock = character.animation().frame(), weight = character.animation().weight();
        check(!character.command({1, 0, false}) && !character.command({1, -1, false}), "Held/directional input restarted the clip");
        near(character.animation().frame(), clock); near(character.animation().weight(), weight);
        ++cases;

        CharacterController cardinal(clips, 1, ids), diagonal(clips, 1, ids), analog(clips, 1, ids);
        cardinal.command({1, 0, false}); diagonal.command({1, 1, false}); analog.command({0.3, 0.4, false});
        cardinal.advance(0.2); diagonal.advance(0.2); analog.advance(0.2);
        near(cardinal.x(), 12); near(std::hypot(diagonal.x(), diagonal.z()), 12);
        near(std::hypot(analog.x(), analog.z()), 6); near(diagonal.speed(), 60);
        ++cases;

        const auto beforeLow = character.animation().current();
        check(character.command({1, -1, true}), "Low posture not selected");
        check(character.state() == CharacterState::Low && character.animation().id() == 33, "Low posture did not override movement");
        samePose(beforeLow, character.animation().current());
        const double stoppedX = character.x(), stoppedZ = character.z(), stoppedYaw = character.yaw();
        character.advance(0.1); near(character.x(), stoppedX); near(character.z(), stoppedZ); near(character.yaw(), stoppedYaw);
        const auto interrupted = character.animation().current(); character.command({0, -1, false});
        samePose(interrupted, character.animation().current());
        check(character.animation().weight() == 0 && character.state() == CharacterState::Moving, "Interruption did not start from visible pose");
        ++cases;

        character.advance(0.1); const double zAtRelease = character.z();
        character.command({0, 0, false});
        for (int i = 0; i < 20; ++i) character.advance(0.1);
        near(character.x(), stoppedX); near(character.z(), zAtRelease); near(character.speed(), 0);
        check(character.state() == CharacterState::Rest && !character.animation().transitioning(), "Stop did not settle in rest");
        ++cases;

        // Ground/root data belongs to the pose; it is applied once, not accumulated.
        const auto worldOrigin = transform({0, 0, 0}, character.world());
        const auto bones = composePose({{-1, {0, 0, 0}}}, character.animation().current());
        const auto composed = transform({1, 0, 0}, multiply(bones[0], character.world()));
        const auto separate = transform(transform({1, 0, 0}, bones[0]), character.world());
        for (unsigned axis = 0; axis < 3; ++axis) near(composed[axis], separate[axis]);
        near(worldOrigin[0], stoppedX); near(worldOrigin[2], zAtRelease); near(worldOrigin[1], 0);
        ++cases;

        // Same commands at the same simulated times, presented at 30 or 60 Hz.
        CharacterController at30(clips, 1, ids), at60(clips, 1, ids);
        for (const auto &input : std::vector<CharacterInput>{{1, 0, false}, {1, -1, false}, {1, 0, true}, {0, 0, false}}) {
            at30.command(input); at60.command(input);
            for (int i = 0; i < 15; ++i) at30.advance(1.0 / 30);
            for (int i = 0; i < 30; ++i) at60.advance(1.0 / 60);
            near(at30.x(), at60.x()); near(at30.z(), at60.z()); near(at30.yaw(), at60.yaw());
            near(at30.animation().frame(), at60.animation().frame()); samePose(at30.animation().current(), at60.animation().current());
        }
        ++cases;

        CharacterController facing(clips, 1, ids);
        facing.command({1, 0, false}); facing.advance(0.25); facing.advance(0.25);
        auto origin = transform({0, 0, 0}, facing.world()), forward = transform({0, 0, -1}, facing.world());
        near(forward[0] - origin[0], 1); near(forward[2] - origin[2], 0);
        facing.command({0.01, 1, false});
        for (int i = 0; i < 4; ++i) facing.advance(0.25);
        const double nearPi = facing.yaw(); facing.command({-0.01, 1, false}); facing.advance(0.01);
        near(std::abs(std::remainder(facing.yaw() - nearPi, 2 * CharacterController::TurnRate)), 2 * std::atan(0.01));
        ++cases;

        auto reject = [&](std::function<void()> f) {
            try { f(); } catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Invalid character input accepted");
        };
        const auto saved = facing.animation().current(); const auto savedWorld = facing.world();
        const auto savedState = facing.state(); const double savedTime = facing.animation().frame(), savedSpeed = facing.speed();
        reject([&] { facing.command({1.01, 0, false}); });
        reject([&] { facing.command({0, -1.01, true}); });
        reject([&] { facing.command({std::numeric_limits<double>::quiet_NaN(), 0, false}); });
        reject([&] { facing.command({0, std::numeric_limits<double>::infinity(), false}); });
        reject([&] { facing.advance(-0.01); });
        reject([&] { facing.advance(0.251); });
        reject([&] { facing.advance(std::numeric_limits<double>::quiet_NaN()); });
        reject([&] { facing.advance(std::numeric_limits<double>::infinity()); });
        samePose(saved, facing.animation().current()); near(savedTime, facing.animation().frame()); near(savedSpeed, facing.speed());
        check(savedWorld == facing.world() && savedState == facing.state(), "Rejected input mutated state");
        reject([&] { CharacterController invalid(clips, 1, {11, 22, 999}); });
        reject([&] { CharacterController invalid(clips, 1, {11, 22, 22}); });
        ++cases;

        std::istringstream valid("# native input\nframes 10\n0 0 0 0\n4 1 -1 1 # low wins\n");
        const auto sequence = readCharacterSequence(valid);
        check(sequence.frames == 10 && sequence.events.size() == 2 && sequence.events[1].input.low, "Valid sequence rejected");
        for (const auto *bad : {"", "frames 181\n0 0 0 0", "frames 10\n1 0 0 0", "frames 10\n0 0 0 0\n0 1 0 0",
                               "frames 10\n0 0 0 2", "frames 10\n0 2 0 0", "frames 10\n0 nan 0 0", "frames 10\n0 0 0 0 extra",
                               "frames 10\n0 0 0 0\n10 0 0 0", "frames 10\n0 0 0 0\n-1 0 0 0"})
            reject([&] { std::istringstream input(bad); readCharacterSequence(input); });
        ++cases;
        std::cout << "{\"status\":\"passed\",\"character_cases\":" << cases << ",\"rejected_cases\":" << rejected << "}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
