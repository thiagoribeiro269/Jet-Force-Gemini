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
        unsigned mathCases = 0, collisionCases = 0, movementCases = 0, rejected = 0;
        auto reject = [&](std::function<void()> f) {
            try { f(); } catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Invalid movement boundary accepted");
        };
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
        uint64_t digest = 0;
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
            ++movementCases;
            // Entry point: fall 21 units onto the path.
            JunoBody body(physics, collision, selection, character->clips, {40, 19, 841}, 0);
            for (int t = 0; t < 30; ++t) body.tick({});
            near(body.position.y, -1.99, 1e-4); check(body.grounded() && body.state568 == 0 && body.move3B == 16, "Entry landing differs");
            ++movementCases;
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
            auto run = [&](uint64_t &hash, JunoBody &juno, JunoCamera &cam) {
                uint32_t jumps = 0, walls = 0; float top = -1e9f;
                for (uint64_t t = 0; t < MovementTicks; ++t) {
                    auto control = movementControl(t);
                    control.cameraYaw = cam.yaw();
                    juno.tick(control);
                    cam.tick(juno, {control.cRight, control.cLeft, control.trigger}, 1);
                    const float dx = juno.position.x - cam.position.x, dz = juno.position.z - cam.position.z;
                    check((dx * dx) + (dz * dz) >= 1023.0f, "Camera left Juno inside its 32-unit push radius");
                    check(cam.position.y >= float(collision->extents[2]) - 100.0f - 1e-3f, "Camera below the track floor limit");
                    for (float v : {juno.position.x, juno.position.y, juno.position.z, juno.velocity.y})
                        check(std::isfinite(v), "Nonfinite movement state");
                    check(juno.position.y > -3, "Juno fell through the path");
                    jumps += juno.state568 == 3; walls += juno.wall533 != 0; top = std::max(top, juno.position.y);
                    uint32_t bits[7] = {}; std::memcpy(bits, &juno.position, 12); std::memcpy(bits + 3, &juno.heading11C, 2);
                    std::memcpy(bits + 4, &cam.position, 12);
                    for (auto word : bits) hash = (hash ^ word) * 1099511628211ull;
                }
                check(jumps > 20 && walls > 10 && top > 50, "Scenario did not jump or reach a wall");
            };
            uint64_t first = 1469598103934665603ull, second = first;
            JunoBody one(physics, collision, selection, character->clips, {40, 19, 841}, 0), two = one;
            JunoCamera cameraOne(cameraData, one), cameraTwo(cameraData, two);
            run(first, one, cameraOne); run(second, two, cameraTwo);
            check(first == second, "Movement is not deterministic");
            digest = first;
            check(one.grounded() && one.state568 == 0 && one.speed04 > -0.25f, "Scenario did not settle on the ground");
            ++movementCases;
            // Session integration: pause, resume, transactions and stop.
            NativeSession session; session.boot(character, selection, region, physics, collision, cameraData);
            session.apply(movementScene(*region));
            const EntityHandle juno{1, 1};
            uint64_t scriptTick = 0; // Host ticks also advance while paused.
            auto source = [&](uint64_t) {
                TickInput input; ActorInput actor; actor.control = movementControl(scriptTick++);
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
            for (uint32_t id : {1019u, 1026u, 1027u, 1028u, 1040u}) check(clips.count(id), "Expected original clip was not played");
            ++movementCases;
            reject([&] {  // Controller values outside the N64 stick range.
                TickInput input; ActorInput actor; actor.control = JunoControl{}; actor.control->stickX = 300;
                input.actors.push_back({juno, actor}); session.apply(input);
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
            reject([&] { JunoControl control; control.trigger = true; JunoBody b = body; b.tick(control); });
            reject([&] { readJunoPhysics(argv[5]); });
            reject([&] { TrackCollision::load(argv[6], physics->math); });
            reject([&] { JunoBody far(physics, collision, selection, character->clips, {1e7f, 0, 0}, 0); });
            reject([&] { JunoControl control; control.stickY = 128; JunoBody b = body; b.tick(control); });
            session.stop();
            check(!session.snapshot().region && !session.body(juno), "Stopped session kept the body");
            actual = true;
        }
        std::cout << "{\"status\":\"passed\",\"math_cases\":" << mathCases << ",\"collision_cases\":" << collisionCases
                  << ",\"movement_cases\":" << movementCases << ",\"rejected_cases\":" << rejected
                  << ",\"original_region\":" << (actual ? "true" : "false") << ",\"scenario_digest\":\"" << std::hex << digest << "\"}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
