#include "animation.h"
#include <iostream>
#include <functional>

using namespace jfg_native;
static void close(float a, float b) { if (std::abs(a - b) > 0.0001f) throw std::runtime_error("Native animation math differs"); }
int main() {
    try {
        std::vector<Bone> bones{{-1, {10, 0, 0}}, {0, {2, 0, 0}}, {1, {1, 0, 0}}};
        auto neutral = pose(bones, nullptr, 0);
        close(transform({1, 0, 0}, neutral[2])[0], 14);
        Clip clip; clip.loop = true;
        Keyframe first{{0, 0, 0}, std::vector<Vec3>(3)}, second = first;
        first.angles[0][2] = Pi / 2;
        second.angles[0][2] = Pi / 2; second.root = {4, 0, 0};
        clip.keys = {first, second};
        auto middle = pose(bones, &clip, 0.5f);
        auto hand = transform({1, 0, 0}, middle[2]);
        close(hand[0], 12); close(hand[1], 4); close(hand[2], 0);
        auto looped = pose(bones, &clip, 2);
        auto start = pose(bones, &clip, 0);
        for (unsigned bone = 0; bone < 3; ++bone) for (unsigned i = 0; i < 16; ++i) close(looped[bone][i], start[bone][i]);
        close(std::sin(interpolateAngle(350 * Pi / 180, 10 * Pi / 180, 0.5f)), 0);
        close(std::cos(interpolateAngle(350 * Pi / 180, 10 * Pi / 180, 0.5f)), 1);
        clip.loop = false;
        auto clamped = pose(bones, &clip, 100);
        close(clamped[0][12], 14);
        auto multiAxis = localMatrix({0.31f, -0.7f, 1.2f}, {0, 0, 0});
        auto unit = transform({1, 0, 0}, multiAxis);
        close(unit[0] * unit[0] + unit[1] * unit[1] + unit[2] * unit[2], 1);
        unsigned rejected = 0;
        auto reject = [&](std::function<void()> call) { try { call(); } catch (const std::runtime_error &) { ++rejected; return; } throw std::runtime_error("Invalid animation accepted"); };
        reject([&] { auto bad = bones; bad[1].parent = 1; pose(bad, nullptr, 0); });
        reject([&] { pose(bones, &clip, -1); });
        reject([&] { Clip empty; pose(bones, &empty, 0); });
        reject([&] { auto bad = clip; bad.keys[0].angles.clear(); pose(bones, &bad, 0); });
        std::cout << "{\"status\":\"passed\",\"math_cases\":6,\"rejected_cases\":" << rejected << "}\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
