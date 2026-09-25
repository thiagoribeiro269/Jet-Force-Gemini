#include "juno_sequence.h"
#include <iostream>
#include <functional>
#include <limits>

using namespace jfg_native;
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void near(double a, double b) { check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 0.0001, "Juno native value differs"); }
static void samePose(const Keyframe &a, const Keyframe &b) {
    check(a.angles.size() == b.angles.size(), "Juno pose sizes differ");
    for (unsigned axis = 0; axis < 3; ++axis) {
        near(a.root[axis], b.root[axis]);
        for (size_t i = 0; i < a.angles.size(); ++i) near(a.angles[i][axis], b.angles[i][axis]);
    }
}
int main(int argc, char **argv) {
    try {
        check(argc == 1 || argc == 2, "usage: check_juno_selection [PRIVATE_TABLE]");
        JunoSelectionData data; data.stationaryThreshold = 0.1f;
        for (unsigned i = 0; i < 52; ++i) data.rows.push_back({{uint8_t(i), uint8_t(i), uint8_t(i)}, {0, 3}, 1000 + i});
        data.rows[0].destinations = {4, 5, 6};
        data.rows[4].transitionProfiles = {1, 2}; data.rows[5].transitionProfiles = {3, 4};
        data.rows[6].transitionProfiles = {5, 6}; data.rows[11].transitionProfiles = {2, 4};
        JunoSelector selector(data);
        struct Choice { float a, b; uint32_t flags, idle, expected; };
        const std::vector<Choice> choices = {
            {0, 0, 0, 17, 17}, {0, 0, 0, 19, 19}, {0, 0, 8, 16, 18}, {0, 0, 2, 16, 18},
            {0, 0, 4, 16, 18}, {0, 0, 15, 19, 16}, {0.1f, 0, 0, 17, 0},
            {std::nextafter(0.1f, 0.f), 0, 0, 17, 17}, {-2, 0, 0, 16, 1}, {1, -2, 16, 16, 9},
            {1, 2, 16, 16, 10}, {2, -2, 0, 16, 1}, {4, 0, 16, 16, 3}, {1.75f, 0, 0, 16, 0},
            {std::nextafter(1.75f, 2.f), 0, 0, 16, 1}, {3.5f, 0, 0, 16, 1},
            {std::nextafter(3.5f, 4.f), 0, 0, 16, 2}, {-0.f, -0.f, 1, 16, 16}
        };
        for (const auto &c : choices) check(selector.choose(junoInput(c.a, c.b, c.flags, c.idle)) == c.expected, "Original choice branch or strict boundary differs");
        unsigned routeCases = 0;
        for (unsigned flags = 0; flags < 8; ++flags) {
            JunoSelectionState state; state.heldObject5CC = flags & 1; state.flag1F4 = flags & 2; state.gunWeight = flags & 4;
            const auto selected = selector.resolve(0, state);
            // Explicit truth table, with differing profiles on every destination.
            const unsigned local[] = {4, 6, 4, 6, 6, 6, 5, 6}, profile[] = {1, 5, 1, 5, 5, 5, 4, 5};
            check(selected.local == local[flags] && selected.clip == 1000 + local[flags] && selected.transitionProfile == profile[flags],
                  "Remap priority or destination profile differs");
            ++routeCases;
        }
        std::vector<Clip> clips;
        for (auto index : {4, 5, 6, 11, 16}) {
            Clip clip; clip.id = 1000 + index; clip.loop = index != 5 && index != 16;
            for (unsigned key = 0; key < 4; ++key) clip.keys.push_back({{float(index + key), 0, 0}, {{0, 0, 0}}});
            clips.push_back(clip);
        }
        JunoAnimationController bridge(data, clips, 1);
        unsigned bridgeCases = 0;
        auto before = bridge.animation().current(); bridge.request(0, {}, 0.5, 0.2);
        samePose(before, bridge.animation().current()); near(bridge.animation().frame(), 2); ++bridgeCases;
        bridge.advance(0.1); before = bridge.animation().current();
        auto heavy = junoInput(0, 0, 68, 16);
        bridge.request(0, heavy, 0.5, 0.2); samePose(before, bridge.animation().current());
        near(bridge.animation().frame(), 1.5); ++bridgeCases;
        bridge.advance(0.1); const auto frame = bridge.animation().frame(); const auto weight = bridge.animation().weight();
        check(!bridge.request(0, heavy, 0, 0.2), "Same remapped clip restarted");
        near(bridge.animation().frame(), frame); near(bridge.animation().weight(), weight); ++bridgeCases;
        bridge.request(11, {}, 0, 0); bridge.advance(0.1); const auto count = bridge.changes();
        const auto sameFrame = bridge.animation().frame();
        check(!bridge.request(11, heavy, 1, 0.2), "Profile-only update restarted a clip");
        check(bridge.selection().transitionProfile == 4 && bridge.changes() == count, "Profile metadata did not update independently");
        near(bridge.animation().frame(), sameFrame); ++bridgeCases;
        bridge.request(0, heavy, 2, 0); near(bridge.animation().frame(), 3);
        near(bridge.animation().current().root[0], 8); ++bridgeCases;
        bridge.request(0, {}, 1, 0); near(bridge.animation().frame(), 0); ++bridgeCases;
        bridge.request(0, heavy, -1, 0); near(bridge.animation().frame(), 0); ++bridgeCases;
        unsigned rejected = 0;
        auto reject = [&](std::function<void()> call) {
            try { call(); } catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Invalid original-selection boundary accepted");
        };
        before = bridge.animation().current(); const auto selectedBefore = bridge.selection();
        const auto changesBefore = bridge.changes();
        reject([&] { bridge.request(52, {}, 0, 0.2); });
        reject([&] { bridge.request(7, {}, 0, 0.2); }); // Known table row, unconverted clip.
        reject([&] { bridge.request(0, {}, std::numeric_limits<double>::quiet_NaN(), 0.2); });
        reject([&] { bridge.request(0, {}, 0, -1); });
        reject([&] { bridge.motion(junoInput(std::numeric_limits<float>::infinity(), 0, 0, 16), 0, 0.2); });
        reject([&] { bridge.motion(junoInput(0, std::numeric_limits<float>::quiet_NaN(), 0, 16), 0, 0.2); });
        reject([&] { selector.choose(junoInput(0, 0, 0, 20)); });
        reject([&] { auto invalid = data; invalid.rows[0].destinations[2] = 52; JunoSelector x(invalid); });
        reject([&] { auto invalid = data; invalid.rows[0].transitionProfiles[0] = 7; JunoSelector x(invalid); });
        reject([&] { auto invalid = data; invalid.rows[0].clip = invalid.rows[1].clip; JunoSelector x(invalid); });
        reject([&] { auto invalid = data; invalid.stationaryThreshold = 0; JunoSelector x(invalid); });
        samePose(before, bridge.animation().current());
        check(bridge.selection().clip == selectedBefore.clip && bridge.selection().transitionProfile == selectedBefore.transitionProfile &&
              bridge.changes() == changesBefore, "Rejected request changed selection state"); ++bridgeCases;
        std::istringstream valid("frames 10\n0 0 0 1 16 0 0.2\n4 1 0 68 16 0.5 0.2\n");
        check(readJunoSequence(valid, data, clips).events.size() == 2, "Valid native sequence failed");
        for (const auto *text : {"", "frames 181\n0 0 0 1 16 0 0.2", "frames 10\n1 0 0 1 16 0 0.2",
                                 "frames 10\n0 0 0 128 16 0 0.2", "frames 10\n0 0 0 0 20 0 0.2",
                                 "frames 10\n0 0 0 1 16 0 0.2\n0 1 0 0 16 0 0.2", "frames 10\n0 nan 0 1 16 0 0.2",
                                 "frames 10\n0 0 0 1 16 0 0.2 extra"})
            reject([&] { std::istringstream input(text); readJunoSequence(input, data, clips); });
        unsigned realCases = 0;
        if (argc == 2) {
            JunoSelector original(readJunoSelection(argv[1]));
            struct Expected { uint32_t request, flags, local, clip, profile; };
            // Small explicit cases reviewed against the source table and branch listing.
            const std::vector<Expected> expected = {
                {0, 0, 0, 1026, 0}, {0, 64, 36, 1061, 0}, {0, 68, 28, 1055, 3}, {0, 100, 36, 1061, 0},
                {4, 0, 44, 1034, 1}, {4, 64, 4, 1033, 1}, {14, 0, 14, 1030, 2}, {14, 68, 33, 1060, 4},
                {16, 0, 16, 1019, 0}, {16, 64, 45, 1024, 0}, {16, 68, 27, 1053, 3}, {19, 0, 16, 1019, 0},
                {20, 0, 17, 1020, 0}, {11, 0, 11, 1031, 2}, {11, 68, 11, 1031, 4}, {26, 0, 3, 1025, 0},
                {51, 0, 51, 1071, 0}, {51, 68, 51, 1071, 3}, {9, 64, 47, 1064, 0}, {9, 68, 31, 1058, 3}
            };
            for (const auto &e : expected) {
                const auto got = original.resolve(e.request, junoInput(0, 0, e.flags, 16));
                check(got.local == e.local && got.clip == e.clip && got.transitionProfile == e.profile, "Audited original selection differs"); ++realCases;
            }
        }
        std::cout << "{\"status\":\"passed\",\"choice_cases\":" << choices.size() << ",\"priority_cases\":" << routeCases
                  << ",\"bridge_cases\":" << bridgeCases << ",\"rejected_cases\":" << rejected << ",\"original_table_cases\":" << realCases << "}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
