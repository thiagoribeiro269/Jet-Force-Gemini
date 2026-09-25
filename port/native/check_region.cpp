#include "region_scenario.h"
#include <iostream>
#include <functional>

using namespace jfg_native;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static void near(double actual, double expected, double tolerance = 0.0001) {
    check(std::isfinite(actual) && std::abs(actual - expected) < tolerance, "Native region/camera value differs");
}
static std::array<float, 4> project(const Vec3 &point, const Matrix &m) {
    std::array<float, 4> result{};
    for (unsigned i = 0; i < 4; ++i) result[i] = point[0] * m[i] + point[1] * m[4 + i] + point[2] * m[8 + i] + m[12 + i];
    return result;
}
int main(int argc, char **argv) {
    try {
        check(argc == 1 || argc == 5, "usage: check_region [CHARACTER SELECTOR REGION_MESH REGION_INFO]");
        unsigned cases = 0, rejected = 0;
        NativeRegion floor;
        floor.triangles = {{{0, 3, 0}, {10, 3, 0}, {0, 3, 10}, 0}};
        check(floor.groundBelow(2, 2, 5).has_value(), "Flat ground not found"); near(floor.groundBelow(2, 2, 5)->height, 3);
        check(!floor.groundBelow(20, 20, 5) && !floor.groundBelow(2, 2, 2), "Ground found outside/above probe"); ++cases;
        floor.triangles = {{{0, 0, 0}, {10, 10, 0}, {0, 20, 10}, 0}};
        near(floor.groundBelow(2, 3, 30)->height, 8); ++cases;
        floor.triangles = {{{0, 0, 0}, {10, 0, 0}, {0, 0, 10}, 0}, {{0, 10, 0}, {10, 10, 0}, {0, 10, 10}, 0},
                           {{2, 0, 2}, {2, 10, 2}, {2, 0, 5}, 0}};
        near(floor.groundBelow(2, 2, 9)->height, 0); near(floor.groundBelow(2, 2, 11)->height, 10);
        near(floor.groundBelow(5, 5, 11)->height, 10); ++cases;
        floor.triangles[1].flags = 0x80; near(floor.groundBelow(2, 2, 11)->height, 0); ++cases;
        WorldCamera camera; camera.eye = {0, 0, 10}; camera.target = {0, 0, 0};
        camera.verticalFov = Pi / 2; camera.aspect = 1; camera.nearPlane = 1; camera.farPlane = 101;
        const auto m = camera.matrix();
        const auto front = project({0, 0, 9}, m), far = project({0, 0, -91}, m);
        near(front[0], 0); near(front[1], 0); near(front[2] / front[3], 0); near(far[2] / far[3], 1);
        near(project({1, 0, 9}, m)[0], 1); near(project({0, 1, 9}, m)[1], 1);
        check(project({0, 0, 11}, m)[3] < 0, "Behind-camera point has positive W"); ++cases;
        camera.eye = {100, 50, -190}; camera.target = {100, 50, -200};
        const auto shifted = project({100, 50, -191}, camera.matrix());
        for (unsigned i = 0; i < 4; ++i) near(shifted[i], front[i]);
        ++cases;
        auto reject = [&](std::function<void()> f) { try { f(); } catch (const std::runtime_error &) { ++rejected; return; } throw std::runtime_error("Invalid region boundary accepted"); };
        reject([&] { auto c = camera; c.target = c.eye; c.matrix(); });
        reject([&] { auto c = camera; c.up = {0, 0, 0}; c.matrix(); });
        reject([&] { auto c = camera; c.verticalFov = 0; c.matrix(); });
        reject([&] { auto c = camera; c.verticalFov = Pi; c.matrix(); });
        reject([&] { auto c = camera; c.aspect = 0; c.matrix(); });
        reject([&] { auto c = camera; c.farPlane = c.nearPlane; c.matrix(); });
        reject([&] { auto c = camera; c.nearPlane = -1; c.matrix(); });
        reject([&] { auto c = camera; c.eye[0] = std::numeric_limits<float>::infinity(); c.matrix(); });
        reject([&] { floor.groundBelow(std::numeric_limits<double>::quiet_NaN(), 0, 1); });
        bool actual = false;
        if (argc == 5) {
            auto character = std::make_shared<const AssetPackage>(loadAssetPackage(argv[1]));
            const auto selection = readJunoSelection(argv[2]); const auto region = loadRegion(argv[3], argv[4]);
            check(region->mesh->textures.size() == 44 && region->mesh->vertices.size() == 6054 && region->triangles.size() == 2342,
                  "Original region counts differ");
            near(region->groundBelow(40, 841, 19)->height, -2);
            check(!region->groundBelow(region->boundsMax[0] + 1000, region->boundsMax[2] + 1000, 1000), "Ground outside original region");
            NativeSession session; session.boot(character, selection, region);
            const auto scene = regionScene(*region, *character, selection); session.apply(scene);
            const auto snapshot = session.snapshot();
            check(snapshot.region && snapshot.renderInstances().size() == 2 && snapshot.entities.size() == 1, "Region did not enter host snapshot");
            near(snapshot.entities[0].x, 40); near(snapshot.entities[0].y, 19); near(snapshot.entities[0].z, 841);
            near(poseMinimumY(*character, snapshot.entities[0].render.bones), -2);
            auto invalid = scene; invalid.scene->regionId = 22;
            reject([&] { session.apply(invalid); }); check(session.snapshot().sceneGeneration == 1 && session.snapshot().region == region, "Failed region change mutated the scene");
            invalid = scene; invalid.scene->entities[0].scale = 0;
            reject([&] { session.apply(invalid); }); check(session.snapshot().sceneGeneration == 1, "Failed model scale mutated the scene");
            session.stop(); check(!session.snapshot().region && !session.snapshot().assets, "Region retained by stopped session");
            actual = true;
        }
        std::cout << "{\"status\":\"passed\",\"geometry_camera_cases\":" << cases << ",\"rejected_cases\":" << rejected
                  << ",\"original_region\":" << (actual ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
