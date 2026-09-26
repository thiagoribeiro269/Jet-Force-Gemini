// Tests for the recovered track collision and Juno movement. Synthetic cases
// pin the ported formulas; real cases use the private Forest First data.
#include "movement_scenario.h"
#include <cstring>
#include <functional>
#include <set>
#include <iostream>

using namespace jfg_native;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static void near(double actual, double expected, double tolerance = 0.0001) {
    if (!(std::isfinite(actual) && std::abs(actual - expected) <= tolerance))
        throw std::runtime_error("Movement value differs: " + std::to_string(actual) + " expected " + std::to_string(expected));
}

// Synthetic tables with the original values for the sampled indices.
static OriginalMath syntheticMath() {
    std::array<float, 1025> sine{};
    std::array<int16_t, 1025> arctan{};
    for (size_t i = 0; i < sine.size(); ++i) {
        sine[i] = float(std::sin(double(i) * 3.14159265358979323846 / 2048));
        arctan[i] = int16_t(std::lround(std::atan(double(i) / 1024) * 65536 / (2 * 3.14159265358979323846)));
    }
    sine[1024] = 1; arctan[1024] = 0x2000;
    return OriginalMath(sine, arctan);
}

// One block: floor at y=0, wall facing -X at x=50, ceiling facing -Y at y=100.
static std::shared_ptr<TrackCollision> syntheticTrack(const OriginalMath &math) {
    auto track = std::make_shared<TrackCollision>();
    track->math = math; track->level = 21; track->geometry = 17; track->polyCapacity = 120; track->edgeCapacity = 80;
    track->extents = {-100, 100, 0, 100, -100, 100};
    track->surfaces = {0xFF};
    TrackBlock block;
    block.box = {-100, 0, -100, 100, 100, 100};
    block.vertices = {{-100, 0, -100}, {-100, 0, 100}, {100, 0, 100}, {100, 0, -100},
                      {50, 0, -100}, {50, 100, -100}, {50, 100, 100}, {50, 0, 100},
                      {-100, 100, -100}, {40, 100, -100}, {40, 100, 100}, {-100, 100, 100}};
    block.faces = {{0, {0, 1, 2}}, {0, {0, 2, 3}}, {0, {0, 2, 1}}, {0, {0, 3, 2}}, {0, {0, 1, 2}}, {0, {0, 2, 3}}};
    block.batches = {{0, 0, 0, 8}, {0, 4, 2, 8}, {0, 8, 4, 8}, {0, 12, 6, 0}};
    block.links = {{0, 0, 0, 1}, {1, 0, 1, 1}, {2, 3, 2, 2}, {3, 3, 3, 2}, {4, 4, 4, 5}, {5, 4, 5, 5}};
    TrackCollision::computeMasks(block);
    check(TrackCollision::computePlanes(block, false) == 21, "Synthetic plane count differs");
    track->blocks.push_back(block);
    return track;
}

int main(int argc, char **argv) {
    try {
        check(argc == 1 || argc == 8, "usage: check_movement [CHARACTER SELECTOR REGION_MESH REGION_INFO COLLISION PHYSICS CAMERA]");
        unsigned mathCases = 0, collisionCases = 0, movementCases = 0, inputCases = 0, rejected = 0, stops = 0;
        auto reject = [&](std::function<void()> f) {
            try { f(); } catch (const NotPortedError &) { throw std::runtime_error("Boundary reported as a gameplay stop");
            } catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Invalid movement boundary accepted");
        };
        // A behaviour the original performs but the port does not: typed stop.
        auto stop = [&](std::function<void()> f, const char *what) {
            try { f(); } catch (const NotPortedError &error) { check(!error.explanation().empty(), what); ++stops; return; }
            throw std::runtime_error(what);
        };
        // controller.c joyRead edges and charControl.c controlReadJoypad.
        {
            JoypadReader reader;
            auto frame = reader.read({Pad::A | Pad::CLeft, 80, -80});
            check(frame.pressed == (Pad::A | Pad::CLeft) && frame.released == 0, "joyRead press edge differs");
            frame = reader.read({Pad::A, 80, -80});
            check(frame.pressed == 0 && frame.released == Pad::CLeft && frame.pad.button == Pad::A, "joyRead held/release edge differs");
            frame = reader.read({Pad::B, 3, -4});
            check(frame.pressed == Pad::B && frame.released == Pad::A, "joyRead swap edge differs");
            auto in = controlReadJoypad(frame, false);
            check(in.keys == Pad::B && in.dkeys == Pad::B && in.released == Pad::A && in.xjoy == 0 && in.yjoy == 0 && in.absX == 3 && in.absY == -4,
                  "controlReadJoypad differs");
            in = controlReadJoypad(reader.read({Pad::Z, 80, -80}), false);
            check(in.xjoy == 65 && in.yjoy == -65 && in.dkeys == Pad::Z, "controlReadJoypad clamp differs");
            in = controlReadJoypad(reader.read({Pad::Z, 80, -80}), true);
            check(in.keys == 0 && in.dkeys == 0 && in.released == 0 && in.xjoy == 0 && in.absX == 0, "disablejoy did not clear the joypad");
            check(joyClamp(4) == 0 && joyClamp(-4) == 0 && joyClamp(5) == 0 && joyClamp(6) == 1 && joyClamp(70) == 65 && joyClamp(-127) == -65,
                  "joyClamp differs");
            reject([&] { JoypadReader other; other.read({0x0040, 0, 0}); });
            reject([&] { JoypadReader other; other.read({0x0080, 0, 0}); });
            inputCases += 4;
        }
        const auto math = syntheticMath();
        // Sinf/Cosf quadrants, mirror step and sign bit.
        check(math.sinf(0) == 0 && math.sinf(0x4000) == 1 && math.sinf(0xC000) == -1 && math.cosf(0) == 1, "Sinf quadrants differ");
        check(std::signbit(math.sinf(0x8000)) && math.sinf(0x2000) == float(std::sin(3.14159265358979323846 / 4)), "Sinf sign/table differ");
        check(math.sinf(0x4001) == math.sinf(0x4010) && math.sinf(0x4001) != math.sinf(0x4000), "Sinf second-quadrant step differs");
        check(math.sinf(0x14000) == math.sinf(0x4000) && math.cosf(-0x4000) == math.cosf(0x4000), "Sinf wraps differently");
        ++mathCases;
        // Arctanf quadrants in binary angles.
        check(math.arctanf(0, 0) == 0 && math.arctanf(0, 1) == 0 && math.arctanf(1, 0) == 0x4000 && math.arctanf(1, 1) == 0x2000, "Arctanf I");
        check(math.arctanf(0, -1) == 0x8000 && math.arctanf(-1, 0) == 0xC000 && math.arctanf(-1, -1) == 0xA000 && math.arctanf(1, -1) == 0x6000,
              "Arctanf II-IV");
        ++mathCases;
        check(OriginalMath::powerf(0.95f, 1) == 0.95f && OriginalMath::powerf(2, -2) == 0.25f && OriginalMath::powerf(7, 0) == 1, "Powerf");
        check(OriginalMath::powerf(0.95f, 3) == (0.95f * 0.95f) * 0.95f, "Powerf multiplication order");
        check(OriginalMath::dAngle(0, 0x7000, 0.5f) == 0x3800 && OriginalMath::dAngle(0x7000, int16_t(0x9000), 0.5f) == int16_t(0x8000),
              "dAngle shortest path or wrap differs");
        check(OriginalMath::dAngle(100, 0, 0.99f) == 1 && OriginalMath::dAngle(0, 100, 0.999f) == 99, "dAngle truncation differs");
        ++mathCases;
        const std::array<int16_t, 3> a{0, 0, 0}, b{10, 0, 0}, c{0, 0, 10};
        check(OriginalMath::xzInTriangle(2, 2, a, b, c) && !OriginalMath::xzInTriangle(8, 8, a, b, c) &&
              !OriginalMath::xzInTriangle(5, 0, a, b, c) && OriginalMath::xzInTriangle(2, 2, a, c, b), "mathXZInTri sign test differs");
        ++mathCases;
        const auto yawed = math.rotateRPY({0x4000, 0, 0}, {0, 0, -1});
        near(yawed.x, -1); near(yawed.z, 0);
        const auto rolled = math.rotateRPY({0, 0, 0x4000}, {0, 13, 0});
        near(rolled.x, -13); near(rolled.y, 0);
        // Angles on 16-step boundaries invert exactly; other angles carry the
        // original table's one-step cosine offset (about 0.1% here).
        const auto back = math.rotateYPR({int16_t(-0x1230), 0, 0}, math.rotateRPY({0x1230, 0, 0}, {3, 0, 4}));
        near(back.x, 3, 1e-5); near(back.z, 4, 1e-5);
        const auto offset = math.rotateYPR({int16_t(-0x1234), 0, 0}, math.rotateRPY({0x1234, 0, 0}, {3, 0, 4}));
        check(std::abs(offset.x - 3) > 1e-4 && std::abs(offset.x - 3) < 1e-2, "Table cosine offset differs");
        ++mathCases;
        // mathRnd from the ROM seed; reference computed independently.
        OriginalRandom random(0x5141564D);
        const int32_t expected[] = {18, 19, 19, 17, 19, 16, 49, 1, 3, 4};
        for (int i = 0; i < 10; ++i) check(random.next(i < 6 ? 16 : i < 8 ? 0 : -5, i < 6 ? 19 : i < 8 ? 99 : 5) == expected[i], "mathRnd differs");
        check(random.seed() == 0x46A64536u, "mathRnd seed differs");
        ++mathCases;

        // Synthetic collision block.
        const auto track = syntheticTrack(math);
        const auto &block = track->blocks[0];
        near(block.planes[0].ny, 1); near(block.planes[0].d, 0); near(block.planes[2].nx, -1); near(block.planes[2].d, 50);
        near(block.planes[4].ny, -1); near(block.planes[4].d, 100);
        check(block.edgeMask[0] == 3 && block.edgeMask[1] == 6, "Exposed edges of the coplanar floor differ");
        check(block.links[1][1] == (0x8000 | 8) && block.links[0][3] == 8, "Shared edge plane sign/reference differs");
        check(block.xzMask[0] == 0xFFFFFFFFu && block.yMask[0] == 1 && block.yMask[2] == 0xFF, "Occupancy masks differ");
        check(TrackCollision::xzCompareMask(block.box, 0, 0, 0, 0) == 0x00800080u && TrackCollision::yCompareMask(block.box, 99, 99) == 0x80,
              "Query masks differ");
        ++collisionCases;
        TrackQuery query(track);
        const Vec3f starts[1] = {{0, 20, 0}}; const Vec3f ends[1] = {{0, 10, 0}}; const float radii[1] = {13};
        check(!query.makePolylist(1, starts, ends, radii, 0xCE002000u, 0x02000000u), "Unexpected polylist overflow");
        check(query.polys.size() == 3 && query.polys[0].marker && query.polys[1].triangle == 0 && query.polys[2].triangle == 1,
              "Floor polylist differs");
        {
            TrackHit hit; Vec3f end{0, 10, 0};
            check(query.planeTest({0, 13.5f, 0}, end, hit, 13, 2, 0), "Floor contact missed");
            near(end.y, 13.01f, 1e-6); check(hit.type == 2 && hit.normal.y == 1 && hit.surface == 0xFF && hit.surfaceFlags == 8, "Floor result");
            near(hit.distance, 0.25, 1e-6);
        }
        ++collisionCases;
        {
            const Vec3f s[1] = {{30, 20, 0}}; const Vec3f e[1] = {{40, 20, 0}};
            query.makePolylist(1, s, e, radii, 0xCE002000u, 0x02000000u);
            TrackHit hit; Vec3f end{40, 20, 0};
            check(query.planeTest({30, 20, 0}, end, hit, 13, 8, 0), "Wall contact missed");
            near(end.x, 40.0f - 3.01f, 1e-5); near(end.y, 20); check(hit.type == 4, "Wall result differs");
        }
        {
            const Vec3f s[1] = {{0, 80, 0}}; const Vec3f e[1] = {{0, 90, 0}};
            query.makePolylist(1, s, e, radii, 0xCE002000u, 0x02000000u);
            TrackHit hit; Vec3f end{0, 90, 0};
            check(query.planeTest({0, 80, 0}, end, hit, 13, 8, 0), "Ceiling contact missed");
            near(end.y, 86.99f, 1e-5); check(hit.type == 8, "Ceiling result differs");
        }
        ++collisionCases;
        float enter = 0, leave = 0;
        check(TrackQuery::cylinderIntersect({0, 0, 0}, {1, 0, 0}, {10, 0, 0}, {0, 0, 1}, 2, enter, leave), "Cylinder missed");
        near(enter, 8); near(leave, 12);
        check(!TrackQuery::cylinderIntersect({0, 0, 0}, {1, 0, 0}, {10, 5, 0}, {0, 0, 1}, 2, enter, leave), "Distant cylinder hit");
        check(!TrackQuery::cylinderIntersect({0, 0, 0}, {0, 0, 1}, {10, 0, 0}, {0, 0, 1}, 2, enter, leave), "Parallel cylinder hit");
        check(TrackQuery::sphereIntersect({0, 0, 0}, {1, 0, 0}, {10, 0, 0}, 2, enter, leave), "Sphere missed");
        near(enter, 8); near(leave, 12);
        ++collisionCases;
        {
            // Floor edge at x=100: a sphere below the floor level sweeping into it.
            const Vec3f s[1] = {{120, -5, 0}}; const Vec3f e[1] = {{95, -5, 0}};
            query.makePolylist(1, s, e, radii, 0xCE002000u, 0x02000000u);
            query.buildEdges();
            check(!query.edges.empty(), "Exposed floor edge missing");
            TrackHit hit; Vec3f end = e[0];
            check(query.edgeTest(s[0], end, hit, 13, 8), "Edge contact missed");
            check(end.x > 95 && (hit.type & 4), "Edge response differs");
        }
        ++collisionCases;
        {
            // Juno-like stack on the floor moving into the wall.
            Vec3f position{20, 0.01f, 0};
            std::array<Vec3f, 3> start{}, end{}, offsets{};
            const std::array<float, 3> r{13, 13, 13};
            const std::array<uint16_t, 3> flags{2, 8, 8};
            for (int k = 0; k < 3; ++k) {
                offsets[k] = {0, 13.0f * float(k + 1), 0};
                start[k] = {position.x + offsets[k].x, position.y + offsets[k].y, position.z};
                end[k] = {start[k].x + 30, start[k].y - 0.45f, start[k].z + 5};
            }
            query.makePolylist(3, start.data(), end.data(), r.data(), 0xCE002000u, 0x02000000u);
            std::array<TrackHit, 3> hits{};
            int32_t nearest = 0; Vec3f pushed;
            const auto mask = query.playerIntersect(position, start.data(), end.data(), r.data(), offsets.data(), flags.data(), hits.data(), 3,
                                                    nearest, pushed);
            check(mask && !(mask & 0xFFFF0000u), "Stack collision mask differs");
            check(position.x <= 37 - 0.009f && position.x > 36 && std::abs(position.y - 0.01f) < 0.02f, "Stack was not stopped by the wall");
            check(nearest == -1 && (hits[0].type & 2), "Stack floor/nearest differs");
        }
        ++collisionCases;
        {
            auto small = std::make_shared<TrackCollision>(*track);
            small->polyCapacity = 3;
            TrackQuery limited(small);
            const Vec3f s[1] = {{0, 50, 0}}; const Vec3f e[1] = {{10, 50, 0}}; const float big[1] = {80};
            check(limited.makePolylist(1, s, e, big, 0, 0) && limited.polys.size() == 3, "Polylist capacity not enforced");
        }
        ++collisionCases;
        reject([&] { TrackQuery bad(nullptr); });
        reject([&] { const Vec3f s[1] = {{std::numeric_limits<float>::quiet_NaN(), 0, 0}}; query.makePolylist(1, s, s, radii, 0, 0); });
        reject([&] { OriginalMath broken({}, {}); });

        bool actual = false;
        uint64_t digest = 0, jointDigest = 0;
        // Camera queries on the synthetic block.
        {
            Vec3f s0{-150, 50, 0}, e0{150, 50, 0};
            float enter = 0, leave = 0;
            check(TrackQuery::clip3D(s0, e0, {-100, 0, -100}, {100, 100, 100}, enter, leave), "Segment through box rejected");
            near(enter, 50.0 / 300.0, 1e-6); near(leave, 250.0 / 300.0, 1e-6); near(s0.x, -100, 1e-4); near(e0.x, 100, 1e-4);
            Vec3f s1{-150, 150, 0}, e1{150, 150, 0};
            check(!TrackQuery::clip3D(s1, e1, {-100, 0, -100}, {100, 100, 100}, enter, leave), "Segment above box accepted");
            TrackQuery::NearestHit hit;
            check(query.nearestIntersection({0, 50, 0}, {80, 50, 0}, hit, 0, 0), "Wall not found by the camera ray");
            near(hit.point.x, 50, 1e-4); near(hit.distance, 50, 1e-4); check(hit.plane.nx == -1.0f && hit.flags == 8, "Ray hit plane differs");
            check(!query.nearestIntersection({0, 50, 0}, {40, 50, 0}, hit, 0, 0), "Ray hit before reaching the wall");
            near(hit.distance, 40, 1e-4);
            check(!query.nearestIntersection({80, 50, 0}, {0, 50, 0}, hit, 0, 0), "Back face stopped the ray");
            const auto floors = query.cylinderHeights(0, 0, -32000, 32000, 16, 0xC00, false);
            const auto ceilings = query.cylinderHeights(0, 0, -32000, 32000, 16, 0xC00, true);
            // The floor diagonal crosses the axis; the ceiling diagonal stays 24.6 units away.
            check(floors.size() == 2 && floors[0].height == 0 && ceilings.size() == 1 && ceilings[0].height == 100 && ceilings[0].ny == -1.0f,
                  "Cylinder heights differ");
            check(query.cylinderHeights(60, 0, -32000, 32000, 16, 0xC00, true).empty() &&
                  query.cylinderHeights(45, 0, -32000, 32000, 16, 0xC00, true).size() == 1, "Cylinder radius against the ceiling edge differs");
            check(TrackQuery::circleTouchesEdge(0, 3, -10, 0, 10, 0, 9.0f) && !TrackQuery::circleTouchesEdge(0, 3.1f, -10, 0, 10, 0, 9.0f) &&
                  TrackQuery::circleTouchesEdge(-12, 0, -10, 0, 10, 0, 4.0f), "Circle against edge differs");
            check(query.cubeBlockList(-200, -10, -200, -96, 10, -96).size() == 1 && query.cubeBlockList(-200, -10, -200, -105, 10, -105).empty(),
                  "Cube block margin differs");
        }
        collisionCases += 3;
        {   // controlCeiling's query: a 15-unit sphere rising 60 units under the y=100 ceiling.
            TrackQuery ceiling(track);
            auto rise = [&](float y) {
                const Vec3f start{0, y, 0}; Vec3f end{0, y + 60.0f, 0}; const float radius = 15.0f;
                ceiling.makePolylist(1, &start, &end, &radius, 0, 0);
                TrackHit hit;
                const bool any = ceiling.getIntersect(start, end, radius, 1, hit);
                return std::make_pair(any && (hit.type & 0x48), end.y);
            };
            const auto low = rise(10.0f), high = rise(30.0f);
            check(!low.first && low.second == 70.0f, "Free rise under the ceiling differs");
            check(high.first && std::abs(high.second - (100.0f - 15.0f - 0.01f)) < 1e-3f, "Ceiling stop differs");
            ++collisionCases;
        }
        if (argc == 8) {
            auto character = std::make_shared<const AssetPackage>(loadAssetPackage(argv[1]));
            const auto selection = readJunoSelection(argv[2]);
            const auto region = loadRegion(argv[3], argv[4]);
            const auto physics = readJunoPhysics(argv[6]);
            const auto collision = TrackCollision::load(argv[5], physics->math);
            const auto cameraData = readJunoCamera(argv[7]);
            size_t planes = 0, exposed = 0;
            for (const auto &b : collision->blocks) {
                planes += b.planes.size();
                for (auto m : b.edgeMask) exposed += (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1);
                for (const auto &link : b.links) for (unsigned k = 1; k < 4; ++k) check((link[k] & 0x7FFF) < b.planes.size(), "Dangling plane");
                for (const auto &p : b.planes) {
                    const double length = std::sqrt(double(p.nx) * p.nx + double(p.ny) * p.ny + double(p.nz) * p.nz);
                    check(std::abs(length - 1) < 1e-5, "Collision plane is not normalized");
                }
            }
            check(planes == 5759 && exposed == 1759, "Forest First collision derivation differs");
            // Ledge bits (+0x20 bits 3..5) come only from ledge-pairing faces;
            // Forest First has none, so trackGetLedgeCrossed never reports one.
            for (const auto &b : collision->blocks) {
                for (auto m : b.edgeMask) check(!(m & 0x38), "Unexpected ledge edge in Forest First");
                for (const auto &f : b.faces) check(!(f.flags & 3), "Unexpected ledge-pairing face in Forest First");
            }
            ++movementCases;
            // Entry point: fall 21 units onto the path.
            JunoBody body(physics, collision, selection, character->clips, {40, 19, 841}, 0);
            for (int t = 0; t < 30; ++t) body.tick({});
            near(body.position.y, -1.99, 1e-4);
            // First func_overlay_16_01004F78: the pistol remaps idle 16 to 45 and
            // switches to overlay profile 0 (radius 15 at 15/30/45) over 5 frames.
            check(body.grounded() && body.state568 == 0 && body.move3B == 45 && body.clipId() == 1024 && body.profile360 == 0 &&
                  body.transition535 == 0 && std::abs(body.sphereBase364[2].y - 45.0f) < 1e-4f && body.sphereDef(0).radius == 15.0f,
                  "Entry landing differs");
            ++movementCases;
            // Drive a copy with raw pads through joyRead, as the session does.
            auto turns = [](const JunoBody &b) {
                std::vector<std::pair<int, int>> list;
                for (size_t i = 0; i < b.jointTurnCount; ++i) list.push_back({b.jointTurns[i].offset, b.jointTurns[i].value});
                return list;
            };
            auto drive = [&](JunoBody &b, JoypadReader &reader, PadState pad, int ticks, uint8_t mode = 0) {
                for (int t = 0; t < ticks; ++t) b.tick({reader.read(pad), 0, mode});
            };
            {   // func_overlay_16_01004934: C-left strafes by 0.2 per frame up to 2.5, move 9.
                JunoBody b = body; JoypadReader reader;
                drive(b, reader, {Pad::CLeft, 0, 0}, 1);
                check(b.lateral10 == -0.2f && b.strafing56C == 1 && b.controlKeys == Pad::CLeft, "Strafe step differs");
                drive(b, reader, {Pad::CLeft, 0, 0}, 14);
                check(b.lateral10 == -2.5f && b.move3B == 47 && b.position.x < 40.0f - 20.0f, "Strafe clamp, move or direction differs");
                const float before = b.lateral10;
                drive(b, reader, {}, 1);
                check(b.strafing56C == 0 && b.lateral10 == before * physics->lateralDecay, "Strafe release decay differs");
                // A jump while strafing is a running jump, even without forward speed.
                drive(b, reader, {Pad::CRight, 0, 0}, 3);
                drive(b, reader, {uint16_t(Pad::CRight | Pad::A), 0, 0}, 1);
                check(b.state568 == 3 && b.move3B == 6 && b.velocity.y > 9.0f, "Strafe running jump differs");
                // Z in the air: boyCanFire refuses, so nothing is fired or stopped.
                drive(b, reader, {Pad::Z, 0, 0}, 5);
                check(b.state568 == 3 && b.firing1F4 == 0, "Fire key in the air differs");
            }
            {   // Expert mode (frontGetTargetControl 1): C-up jumps, A selects weapons (one weapon: inert).
                JunoBody b = body; JoypadReader reader;
                drive(b, reader, {Pad::A, 0, 0}, 2, 1);
                check(b.state568 == 0, "A jumped in Expert mode");
                drive(b, reader, {}, 1, 1);
                drive(b, reader, {Pad::CUp, 0, 0}, 1, 1);
                check(b.state568 == 3 && b.move3B == 5, "C-up did not jump in Expert mode");
            }
            {   // A half-turn skid starts first; then Z is held. Z cancels the
                // half-turn (Normal mode), boyCanFire refuses while the skid lasts
                // and +0x1F4 holds 0xF; once firing is allowed the shot stops.
                JunoBody b = body; JoypadReader reader;
                drive(b, reader, {0, 0, 70}, 60);
                drive(b, reader, {0, 0, -70}, 1);
                check(b.halfTurn13E == 1 && b.skid576 == 2, "Half-turn skid did not start");
                bool blocked = false, firingProfile = false;
                stop([&] {
                    for (int t = 0; t < 120; ++t) {
                        b.tick({reader.read({Pad::Z, 0, -70}), 0, 0});
                        blocked = blocked || (b.firing1F4 == 0xF && (b.skid576 != 0 || b.halfTurn13E != 0));
                        // Firing column of the remap: profile 3 keeps the gun sphere (skip mask 0x08).
                        firingProfile = firingProfile || (b.profile360 == 3 && b.skip531 == 0x08 && b.sphereDef(4).offset.z == -32.0f);
                    }
                }, "Pistol shot did not stop the session");
                check(blocked && firingProfile, "boyCanFire or the firing profile differs during the skid");
            }
            stop([&] { JunoBody b = body; JoypadReader r; drive(b, r, {Pad::Z, 0, 0}, 1); }, "Standing shot did not stop");
            {   // Standing aim, state 0xB (0x4440) with controlGetManualAim and the aim camera.
                JunoBody b = body; JunoCamera cam(cameraData, b); JoypadReader r;
                cam.offset10A = 0x0400;  // a C-button offset left by the free camera
                const int16_t before = b.heading11C;
                stepJuno(b, &cam, r.read({Pad::R, 0, 0}), 0);
                check(b.state568 == 0xB && b.heading11C == int16_t(before - 0x0400) && cam.offset10A == 0, "Aim entry differs");
                for (int t = 0; t < 30; ++t) stepJuno(b, &cam, r.read({Pad::R, 0, 0}), 0);
                check(b.firing1F4 == 1 && b.move3B == 27 && b.profile360 == 3 && cam.fov > 55.0f && cam.fov < 56.0f,
                      "Aim stance or aim camera blend differs");
                // Stick past 45 turns Juno; within 40 it only moves the aim.
                const int16_t steady = b.heading11C;
                for (int t = 0; t < 5; ++t) stepJuno(b, &cam, r.read({Pad::R, 40, 0}), 0);
                check(b.heading11C == steady && b.joint1CE != 0, "Aim within the dead band turned Juno");
                {   // 0x3DB0 at the end of 0x4440: half the pitch on the torso, the yaw, the rest on channel 9,
                    // then the head/arm against the recoil turns (equal to the aim turns without shots).
                    const int32_t half = b.joint1D0 / 2;
                    check(turns(b) == std::vector<std::pair<int, int>>{{6, half}, {8, b.joint1CE}, {0x12, b.joint1D0 - half},
                                                                      {0x3C, b.joint1DE - half}, {0x3E, 0}, {0x44, int16_t(b.twist580)}} &&
                          b.joint1CE != 0 && b.joint1DC == b.joint1CE && b.joint1DE == b.joint1D0, "Standing aim joint turns differ");
                }
                for (int t = 0; t < 20; ++t) stepJuno(b, &cam, r.read({Pad::R, 80, 0}), 0);
                check(b.heading11C != steady && b.turn1E4 < 0.0f, "Aim at the edge did not turn Juno");
                for (int t = 0; t < 120; ++t) stepJuno(b, &cam, r.read({Pad::R, 0, 80}), 0);
                check(b.aimPitch1E2 == -0x2AAA, "Aim pitch limit differs");
                // Aiming past the limit: 0x3DB0 clamps both pitches to -0x2AAA.
                check(b.joint1D0 < -0x2AAA && turns(b)[0] == std::pair<int, int>{6, -0x1555} && turns(b)[2] == std::pair<int, int>{0x12, -0x1555} &&
                      turns(b)[3] == std::pair<int, int>{0x3C, -0x1555}, "Aim joint-turn clamp differs");
                // After 175 frames +0x18 is near 1: field of view near 60 and the camera at the aim distance.
                const float dx = cam.position.x - b.position.x, dz = cam.position.z - b.position.z;
                check(cam.fov > 59.7f && std::sqrt(dx * dx + dz * dz) < 70.0f, "Aim camera differs");
                stop([&] { JunoBody c = b; JunoCamera k = cam; JoypadReader q = r; stepJuno(c, &k, q.read({uint16_t(Pad::R | Pad::Z), 0, 0}), 0); },
                     "Shot while aiming did not stop");
                // Releasing R: walking again, the free camera's orbit behind Juno.
                const int16_t pitchBefore = b.joint1D0;
                stepJuno(b, &cam, r.read({}), 0);
                check(b.state568 == 0 && cam.orbit104 == int16_t(0x8000 - b.orientation[0]), "Aim exit differs");
                // Leaving the aim, +0x1F4 is still 1 for this frame: 0x6290 eases the turns (>>2
                // toward the clamped aim, >>4 for recoil) and 0x3DB0 still writes them. Then only the twist.
                check(b.firing1F4 == 1 && b.jointTurnCount == 6 && b.joint1D0 == int16_t(pitchBefore + shiftRight(-0x2AAA - pitchBefore, 2)) &&
                      b.joint1DE == int16_t(pitchBefore + shiftRight(-pitchBefore, 4)), "Joint turns leaving the aim differ");
                stepJuno(b, &cam, r.read({}), 0);
                check(b.firing1F4 == 0 && turns(b) == std::vector<std::pair<int, int>>{{0x44, int16_t(b.twist580)}}, "Joint turns after the aim differ");
                // Expert: C-up walks forward while aiming.
                JunoBody e = body; JoypadReader q;
                drive(e, q, {Pad::R, 0, 0}, 5, 1);
                drive(e, q, {uint16_t(Pad::R | Pad::CUp), 0, 0}, 10, 1);
                check(e.state568 == 0xB && e.speed04 < -1.0f, "Expert aim walk differs");
                ++movementCases;
            }
            {   // Joint turns outside the aim (0x6290, 0x3DB0, 0x4840) and their use by gen_anim_data.
                JunoBody b = body; JoypadReader r;
                drive(b, r, {}, 1);
                check(turns(b) == std::vector<std::pair<int, int>>{{0x44, 0}}, "Idle joint turns differ");
                // +0x1F4 still set (a held fire key 2 frames ago): 0x3DB0 halves the eased pitch with
                // round-toward-zero, -9 -> -4 and -5, and the recoil pitch 0 leaves +4 on channel 30.
                b.firing1F4 = 2; b.joint1D0 = -12; b.joint1CE = 0; b.joint1DE = 0; b.joint1DC = 0;
                drive(b, r, {}, 1);
                check(b.firing1F4 == 1 && b.joint1D0 == -9 && turns(b) == std::vector<std::pair<int, int>>{{6, -4}, {8, 0}, {0x12, -5}, {0x3C, 4},
                                                                                                          {0x3E, 0}, {0x44, 0}},
                      "0x3DB0 rounding differs");
                // Turning while running: the aim follows the body one frame late, so the yaw turn lags
                // (clamped to 0x1000) even though nothing writes it to the list without +0x1F4.
                drive(b, r, {0, 0, 70}, 40);
                bool lag = false;
                for (int t = 0; t < 20; ++t) { drive(b, r, {0, 70, 30}, 1); lag = lag || (b.joint1CE != 0 && b.joint1CE >= -0x1000 && b.joint1CE <= 0x1000); }
                check(lag && b.firing1F4 == 0 && b.jointTurnCount == 1, "Joint-turn lag while turning differs");
                // Running with a C-left strafe: 0x4840 twists the torso toward -Arctanf(lateral, |speed|),
                // 1/16 of the wrapped difference per frame, and it settles within 16 of the target.
                drive(b, r, {0, 0, 70}, 60);
                drive(b, r, {Pad::CLeft, 0, 70}, 240);
                check(b.lateral10 < 0.0f && std::abs(b.lateral10) < std::abs(b.speed04), "Running strafe differs");
                int32_t target = -int32_t(physics->math.arctanf(b.lateral10, std::abs(b.speed04))) & 0xFFFF;
                if (target >= 0x8000) target -= 0x10000;
                check(target > 0 && std::abs(b.twist580 - target) < 16 && turns(b).back() == std::pair<int, int>{0x44, int16_t(b.twist580)},
                      "Strafe torso twist differs");
                // Sideways faster than forward: the twist returns to zero.
                drive(b, r, {Pad::CLeft, 0, 0}, 120);
                check(std::abs(b.lateral10) >= std::abs(b.speed04) && std::abs(b.twist580) < 16, "Strafe twist release differs");
                // gen_anim_data: channel o/2 is bone o/6, axis (o/2)%3, in binary angle units.
                Keyframe pose{{0, 0, 0}, std::vector<Vec3>(21, Vec3{0.25f, 0.5f, 0.75f})};
                JunoBody list = body;
                list.jointTurns = {{{6, 0x4000}, {8, -0x2000}, {0x12, 0x1000}, {0x3C, 0}, {0x3E, 0x800}, {0x44, -0x100}}};
                list.jointTurnCount = 6;
                applyJointTurns(pose, list);
                near(pose.angles[1][0], 0.25 + Pi / 2, 1e-6); near(pose.angles[1][1], 0.5 - Pi / 4, 1e-6); near(pose.angles[3][0], 0.25 + Pi / 8, 1e-6);
                near(pose.angles[10][0], 0.25, 0); near(pose.angles[10][1], 0.5 + Pi / 16, 1e-6); near(pose.angles[11][1], 0.5 - Pi / 128, 1e-6);
                near(pose.angles[0][0], 0.25, 0); near(pose.angles[11][2], 0.75, 0);
                for (uint16_t offset : {uint16_t(0x1006), uint16_t(7), uint16_t(120)})
                    reject([&] { JunoBody bad = body; bad.jointTurns[0] = {offset, 1}; bad.jointTurnCount = 1; Keyframe k = pose; applyJointTurns(k, bad); });
                reject([&] { JunoBody bad = body; bad.jointTurnCount = 9; Keyframe k = pose; applyJointTurns(k, bad); });
                reject([&] { Keyframe small{{0, 0, 0}, std::vector<Vec3>(10)}; applyJointTurns(small, list); });
                movementCases += 2;
            }
            {   // Crouch states: 0x2EB4 (1) and 0x321C (2) with their collision profiles.
                JunoBody b = body; JoypadReader r;
                drive(b, r, {Pad::B, 0, 0}, 1);
                check(b.state568 == 1 && b.move3B == 13 && b.profile360 == 2 && b.skip531 == 0x1C, "Crouch down differs");
                drive(b, r, {}, 60);
                check(b.state568 == 1 && b.move3B == 14 && b.clipId() == 1030, "Crouched idle differs");
                {   // Z crouched: +0x1F4 remaps 14 to 33, whose 2-step blend delays the shot.
                    JunoBody c = b; JoypadReader q = r; int ticks = 0;
                    stop([&] { for (; ticks < 10; ++ticks) drive(c, q, {Pad::Z, 0, 0}, 1); }, "Crouched shot did not stop");
                    check(ticks >= 1 && ticks <= 3 && c.move3B == 33, "Crouched shot timing differs");
                }
                {   // Crouched aim, state 5 (0x3F30), once the clip blend (+0x5E) has ended.
                    JunoBody c = b; JunoCamera k(cameraData, c); JoypadReader q = r;
                    check(c.blend5E == 0, "Crouched blend did not end");
                    stepJuno(c, &k, q.read({Pad::R, 0, 0}), 0);
                    check(c.state568 == 5, "Crouched aim did not start");
                    for (int t = 0; t < 30; ++t) stepJuno(c, &k, q.read({Pad::R, 0, 80}), 0);
                    check(c.state568 == 5 && c.aimPitch1E2 == -0xA80 && k.fov > 55.0f, "Crouched aim limits or camera differ");
                    // 0x3F30's own list: no half pitch and no clamp.
                    check(turns(c) == std::vector<std::pair<int, int>>{{6, c.joint1D0}, {8, c.joint1CE}, {0x12, 0}, {0x3C, int16_t(c.joint1DE - c.joint1D0)},
                                                                      {0x3E, int16_t(c.joint1DC - c.joint1CE)}, {0x44, int16_t(c.twist580)}} &&
                          c.joint1D0 < -0xA80, "Crouched aim joint turns differ");
                    JunoBody up = c; JunoCamera ku = k; JoypadReader qu = q;
                    stepJuno(up, &ku, qu.read({uint16_t(Pad::R | Pad::A), 0, 0}), 0);
                    check(up.state568 == 0xB, "A in crouched aim did not stand into the aim");
                    stepJuno(c, &k, q.read({}), 0);
                    check(c.state568 == 1 && k.orbit104 == int16_t(0x8000 - c.orientation[0]), "Crouched aim exit differs");
                }
                drive(b, r, {0, 0, 70}, 40);
                check(b.state568 == 2 && b.move3B == 4 && b.profile360 == 1 && b.profileMask(1) == 7 && std::abs(b.speed04) <= 1.25f &&
                      std::abs(b.speed04) > 0.5f, "Crouch-walk differs");
                drive(b, r, {Pad::Z, 0, 70}, 3);  // boyCanFire refuses while crouch-walking
                check(b.state568 == 2 && b.firing1F4 == 0, "Crouch-walk fire key differs");
                // Roll: +0x584 = 3, move 0x32, sideways speed from the first roll curve.
                drive(b, r, {Pad::CLeft, 0, 70}, 1);
                check(b.roll584 == 3 && b.move3B == 50 && b.strafing56C == 1 && b.lateral10 < 0.0f, "Crouch-walk roll start differs");
                int ticks = 0;
                while (b.roll584 && ticks < 200) { drive(b, r, {0, 0, 70}, 1); ++ticks; }
                check(ticks > 10 && ticks < 60 && b.move3B == 4 && b.lateral10 == 0.0f, "Crouch-walk roll end differs");
                // A while crouch-walking with room overhead: stand up with move 8.
                drive(b, r, {Pad::A, 0, 0}, 1);
                check(b.state568 == 0 && b.move3B == 8 && b.profile360 == 0, "Stand up differs");
                drive(b, r, {}, 40);
                check(b.state568 == 0 && b.move3B != 8, "Stand-up clip did not end");
            }
            {   // Slide: B while running faster than 2 enters state 1 with a 40-frame slide.
                JunoBody b = body; JoypadReader r;
                drive(b, r, {0, 0, 70}, 60);
                drive(b, r, {Pad::B, 0, 70}, 1);
                check(b.state568 == 1 && b.move3B == 15 && b.skid576 == 0x28, "Slide start differs");
                drive(b, r, {}, 39);
                check(b.state568 == 1 && b.skid576 == 1, "Slide timer differs");
                drive(b, r, {}, 1);
                check(b.state568 == 1 && b.skid576 == 0 && b.move3B == 14 && b.speed04 == 0.0f, "Slide end differs");
                // Right after the slide the crouched clip is still blending: R waits.
                {
                    JunoBody c = b; JoypadReader q = r;
                    check(c.move3B == 14 && c.blend5E > 0, "Crouched clip blend did not start");
                    int waited = 0;
                    while (c.state568 == 1 && waited < 20) { drive(c, q, {Pad::R, 0, 0}, 1); ++waited; }
                    check(c.state568 == 5 && waited > 1 && waited <= 6, "Crouched aim did not wait for the clip blend");
                }
                // Crouched roll with C-right: +0x584 = 2, move 0xB; A during the roll does not stand.
                drive(b, r, {Pad::CRight, 0, 0}, 1);
                check(b.roll584 == 2 && b.move3B == 11 && b.lateral10 > 0.0f, "Crouched roll differs");
                drive(b, r, {Pad::A, 0, 0}, 5);
                check(b.state568 == 1 && b.roll584 == 2, "Standing during a roll differs");
            }
            {   // Expert mode: C-down crouches, C-up stands.
                JunoBody b = body; JoypadReader r;
                drive(b, r, {Pad::CDown, 0, 0}, 1, 1);
                check(b.state568 == 1 && b.move3B == 13, "Expert crouch differs");
                drive(b, r, {}, 60, 1);
                drive(b, r, {Pad::CUp, 0, 0}, 1, 1);
                check(b.state568 == 0, "Expert stand differs");
            }
            movementCases += 3;
            {   // Inert with one weapon or no reader: C-up/C-down, D-pad, L, Start in Normal mode.
                JunoBody b = body, reference = body; JoypadReader r, q;
                drive(b, r, {uint16_t(Pad::CUp | Pad::CDown | Pad::Up | Pad::Down | Pad::Left | Pad::Right | Pad::L | Pad::Start), 0, 0}, 40);
                drive(reference, q, {}, 40);
                check(b.position == reference.position && b.move3B == reference.move3B && b.progress28 == reference.progress28,
                      "Buttons the original ignores changed Juno");
            }
            {   // Landing lock (disablejoy): the keys the camera reads are cleared too.
                JunoBody b = body; JoypadReader r;
                b.landingLock56B = 5;
                drive(b, r, {uint16_t(Pad::CLeft | Pad::R | Pad::Z), 0, 70}, 1);
                check(b.controlKeys == 0 && b.controlXjoy == 0 && b.lateral10 == 0.0f, "disablejoy leaked controller input");
            }
            movementCases += 5;
            // Scripted run, jump, wall, turn and bank: deterministic and bounded.
            // Original camera right after spawning: behind Juno, above the path.
            {
                JunoBody fresh(physics, collision, selection, character->clips, {40, 19, 841}, 0);
                JunoCamera cam(cameraData, fresh);
                const float dx = cam.position.x - fresh.position.x, dz = cam.position.z - fresh.position.z;
                check(cam.orbit104 == int16_t(0x8000) && dz > 100 && std::abs(dx) < 1 && cam.position.y > fresh.position.y && cam.fov == 52.0f,
                      "Original camera did not start behind Juno");
            }
            ++movementCases;
            auto run = [&](uint64_t &hash, uint64_t &jointHash, JunoBody &juno, JunoCamera &cam) {
                uint32_t jumps = 0, walls = 0, strafeTicks = 0, crouched = 0, crouchWalk = 0, rolls = 0, slides = 0, aiming = 0, crouchAim = 0; float top = -1e9f;
                uint32_t aimTurns = 0, crouchAimTurns = 0, aimExits = 0, twisted = 0;
                JoypadReader reader;
                for (uint64_t t = 0; t < MovementTicks; ++t) {
                    stepJuno(juno, &cam, reader.read(movementPad(t)), 0);
                    strafeTicks += juno.move3B == 47 && juno.lateral10 < -2.0f;
                    crouched += juno.state568 == 1 && juno.move3B == 14;
                    crouchWalk += juno.state568 == 2 && juno.move3B == 4;
                    rolls += juno.roll584 != 0;
                    slides += juno.state568 == 1 && juno.move3B == 15;
                    aiming += juno.state568 == 0xB;
                    crouchAim += juno.state568 == 5;
                    const float dx = juno.position.x - cam.position.x, dz = juno.position.z - cam.position.z;
                    check((dx * dx) + (dz * dz) >= 1023.0f, "Camera left Juno inside its 32-unit push radius");
                    check(cam.position.y >= float(collision->extents[2]) - 100.0f - 1e-3f, "Camera below the track floor limit");
                    for (float v : {juno.position.x, juno.position.y, juno.position.z, juno.velocity.y})
                        check(std::isfinite(v), "Nonfinite movement state");
                    check(juno.position.y > -3, "Juno fell through the path");
                    jumps += juno.state568 == 3; walls += juno.wall533 != 0; top = std::max(top, juno.position.y);
                    // Joint turns: 0x3DB0 in the standing aim, 0x3F30's list crouched, one 0x3DB0 frame on
                    // each aim exit, and the 0x4840 twist always last.
                    const auto list = turns(juno);
                    check(!list.empty() && list.back() == std::pair<int, int>{0x44, int16_t(juno.twist580)}, "Scenario twist entry differs");
                    aimTurns += juno.state568 == 0xB && list.size() == 6 && list[0].first == 6;
                    crouchAimTurns += juno.state568 == 5 && list.size() == 6 && list[2] == std::pair<int, int>{0x12, 0};
                    aimExits += juno.state568 == 0 && list.size() == 6;
                    twisted += juno.twist580 != 0;
                    for (const auto &entry : list) jointHash = (jointHash ^ uint32_t(entry.first << 16 | uint16_t(entry.second))) * 1099511628211ull;
                    uint32_t bits[7] = {}; std::memcpy(bits, &juno.position, 12); std::memcpy(bits + 3, &juno.heading11C, 2);
                    std::memcpy(bits + 4, &cam.position, 12);
                    for (auto word : bits) hash = (hash ^ word) * 1099511628211ull;
                }
                check(jumps > 20 && walls > 10 && top > 50, "Scenario did not jump or reach a wall");
                check(strafeTicks > 20, "Scenario did not strafe with C-left");
                check(crouched > 10 && crouchWalk > 40 && rolls > 20 && slides == 40, "Scenario did not crouch, roll or slide");
                check(aiming >= 105 && crouchAim >= 35 && juno.state568 == 0, "Scenario did not aim or crouch-aim");
                // The frame that enters an aim state from walking or crouching runs the old state's
                // routine, which writes no aim list (A from the crouched aim keeps 0x3F30's list).
                // Rolls and running strafes twist the torso.
                check(aimTurns == aiming - 1 && crouchAimTurns == crouchAim - 1 && aimExits == 2 && twisted > 150, "Scenario joint turns differ");
            };
            uint64_t first = 1469598103934665603ull, second = first, joints = first, jointsTwo = first;
            JunoBody one(physics, collision, selection, character->clips, {40, 19, 841}, 0), two = one;
            JunoCamera cameraOne(cameraData, one), cameraTwo(cameraData, two);
            run(first, joints, one, cameraOne); run(second, jointsTwo, two, cameraTwo);
            check(first == second && joints == jointsTwo, "Movement is not deterministic");
            digest = first; jointDigest = joints;
            check(one.grounded() && one.state568 == 0 && one.speed04 > -0.25f, "Scenario did not settle on the ground");
            ++movementCases;
            // Session integration: pause, resume, transactions and stop.
            NativeSession session; session.boot(character, selection, region, physics, collision, cameraData);
            session.apply(movementScene(*region));
            const EntityHandle juno{1, 1};
            uint64_t scriptTick = 0; // Host ticks also advance while paused.
            auto source = [&](uint64_t) {
                TickInput input; ActorInput actor; actor.pad = movementPad(scriptTick++);
                input.actors.push_back({juno, actor}); return input;
            };
            std::set<uint32_t> clips;
            for (int i = 0; i < 20; ++i) session.advanceNanoseconds(NativeSession::ClockScale / 60, source);
            const auto before = session.body(juno)->position;
            TickInput pause; pause.pause = true; session.apply(pause);
            session.advanceNanoseconds(NativeSession::ClockScale / 10, [](uint64_t) { return TickInput{}; });
            check(session.body(juno)->position == before, "Paused body moved");
            TickInput resume; resume.pause = false; session.apply(resume);
            const auto snapshot = session.snapshot();
            check(snapshot.entities.size() == 1 && snapshot.entities[0].y == double(before.y), "Snapshot position differs from body");
            // Whole script through the session: the animation follows the move machine.
            for (uint64_t t = 20; t < MovementTicks; ++t) {
                session.advanceNanoseconds(NativeSession::ClockScale / 60 + 1, source);
                clips.insert(session.snapshot().entities[0].clip);
            }
            check(scriptTick == MovementTicks, "Session did not run the whole script");
            check(session.body(juno)->position == one.position && session.camera(juno)->position == cameraOne.position,
                  "Session and bare body/camera diverged");
            // Gun-held variants: idle 45, walk 36, run 35/34, strafe 47; running jump 6.
            for (uint32_t id : {1024u, 1061u, 1062u, 1063u, 1064u, 1040u, 1029u, 1030u, 1033u, 1037u, 1038u, 1044u, 1053u, 1059u})
                check(clips.count(id), "Expected original clip was not played");
            // Walk, run and strafe never lose the gun-held remap. (Idle 1019 can
            // appear for one frame: leaving the aim, row 27 remaps to 16, then 45.)
            for (uint32_t id : {1026u, 1027u, 1028u, 1042u}) check(!clips.count(id), "Clip without the pistol remap was played");
            ++movementCases;
            reject([&] {  // Controller bits outside the standard N64 buttons.
                TickInput input; ActorInput actor; actor.pad = PadState{0x0080, 0, 0};
                input.actors.push_back({juno, actor}); session.apply(input);
            });
            reject([&] {  // Unknown menu control mode.
                TickInput input; SpawnSpec spawn{2, 40, 19, 841, 0, {}, 1, 0}; spawn.originalBody = true; spawn.controlMode = 2;
                input.spawn.push_back(spawn); session.apply(input);
            });
            reject([&] {  // Provisional planar commands on an original body.
                TickInput input; ActorInput actor; actor.movement.x = 1; input.actors.push_back({juno, actor}); session.apply(input);
            });
            reject([&] {  // Original body outside a region scene.
                TickInput input; SpawnSpec spawn{2, 0, 0, 0, 0, {}, 1, 0}; spawn.originalBody = true;
                input.scene = SceneSpec{"Empty", false, {spawn}}; session.apply(input);
            });
            check(session.snapshot().sceneGeneration == 1, "Rejected command changed the scene");
            reject([&] { NativeSession other; other.boot(character, selection, {}, physics, collision); });
            reject([&] { NativeSession other; other.boot(character, selection, region, physics, {}); });
            reject([&] { NativeSession other; other.boot(character, selection, region, {}, {}, cameraData); });
            reject([&] { readJunoCamera(argv[6]); });
            reject([&] { JunoControl control; control.controlMode = 2; JunoBody b = body; b.tick(control); });
            reject([&] { readJunoPhysics(argv[5]); });
            reject([&] { TrackCollision::load(argv[6], physics->math); });
            reject([&] { JunoBody far(physics, collision, selection, character->clips, {1e7f, 0, 0}, 0); });
            reject([&] { JunoControl control; control.joypad.pad.button = 0x0040; JunoBody b = body; b.tick(control); });
            session.stop();
            check(!session.snapshot().region && !session.body(juno), "Stopped session kept the body");
            actual = true;
        }
        std::cout << "{\"status\":\"passed\",\"math_cases\":" << mathCases << ",\"input_cases\":" << inputCases
                  << ",\"collision_cases\":" << collisionCases << ",\"movement_cases\":" << movementCases
                  << ",\"not_ported_stops\":" << stops << ",\"rejected_cases\":" << rejected
                  << ",\"original_region\":" << (actual ? "true" : "false") << ",\"scenario_digest\":\"" << std::hex << digest << "\",\"joint_digest\":\"" << jointDigest << "\"}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
