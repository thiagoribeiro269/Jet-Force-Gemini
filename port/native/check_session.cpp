#include "integration_scenario.h"
#include <iostream>
#include <limits>

using namespace jfg_native;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static void near(double a, double b) { check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 0.00001, "Native session value differs"); }
static void sameEntities(const SessionSnapshot &a, const SessionSnapshot &b) {
    check(a.entities.size() == b.entities.size(), "Different native entity count");
    for (size_t i = 0; i < a.entities.size(); ++i) {
        const auto &x = a.entities[i]; const auto &y = b.entities[i];
        check(x.handle == y.handle && x.clip == y.clip && x.transitionProfile == y.transitionProfile, "Different native entity state");
        near(x.x, y.x); near(x.y, y.y); near(x.z, y.z); near(x.yaw, y.yaw); near(x.animationFrame, y.animationFrame);
        check(x.render.bones == y.render.bones, "Native poses depend on presentation cadence");
    }
}
static void same(const SessionSnapshot &a, const SessionSnapshot &b) {
    check(a.phase == b.phase && a.hostTick == b.hostTick && a.worldTick == b.worldTick && a.sceneGeneration == b.sceneGeneration &&
          a.sceneName == b.sceneName, "Different native application state");
    sameEntities(a, b);
}
static std::vector<SessionSnapshot> runCadence(std::shared_ptr<const AssetPackage> assets, const JunoSelectionData &table, unsigned fps) {
    NativeSession session; session.boot(std::move(assets), table); session.apply(integrationInitial());
    uint64_t previous = 0;
    std::vector<SessionSnapshot> checkpoints;
    for (unsigned frame = 1; frame <= 6 * fps; ++frame) {
        const auto time = presentationTime(frame, fps);
        session.advanceNanoseconds(time - previous, integrationInput); previous = time;
        if ((frame * 2) % fps == 0) checkpoints.push_back(session.snapshot());
    }
    auto result = session.snapshot();
    check(result.hostTick == 360 && result.worldTick == 120 && result.sceneGeneration == 2 && result.entities.size() == 1,
          "Integration did not complete its scene/clock lifecycle");
    near(result.entities[0].x, 116); check(result.entities[0].handle == EntityHandle{2, 2}, "Wrong surviving entity");
    session.stop(); check(session.snapshot().entities.empty() && !session.snapshot().assets, "Stop retained the world/resources");
    check(checkpoints.size() == 12, "Missing common cadence checkpoints");
    return checkpoints;
}
int main(int argc, char **argv) {
    try {
        check(argc == 1 || argc == 3, "usage: check_session [SCENE SELECTOR]");
        auto assets = std::make_shared<AssetPackage>(); assets->rigged = assets->multipleClips = true;
        assets->skeleton = {{-1, {0, 0, 0}}};
        JunoSelectionData table; table.stationaryThreshold = 0.1f;
        for (unsigned i = 0; i < 52; ++i) table.rows.push_back({{uint8_t(i), uint8_t(i), uint8_t(i)}, {0, 3}, 2000 + i});
        const std::vector<std::pair<unsigned, unsigned>> ids{{0, 1026}, {1, 1027}, {2, 1028}, {3, 1025}, {14, 1030}, {16, 1019}, {28, 1055}, {36, 1061}, {51, 1071}};
        for (auto [index, id] : ids) {
            table.rows[index].clip = id;
            Clip clip; clip.id = id; clip.loop = id != 1019 && id != 1071;
            clip.keys = {{{0, 0, 0}, {{0, 0, 0}}}, {{1, 0, 0}, {{0, 0, 0.1f}}}};
            assets->clips.push_back(clip);
        }
        unsigned cases = 0, rejected = 0;
        auto reject = [&](std::function<void()> f) { try { f(); } catch (const std::runtime_error &) { ++rejected; return; } throw std::runtime_error("Invalid native session operation accepted"); };
        NativeSession session;
        check(session.snapshot().phase == SessionPhase::Cold, "Session did not start cold");
        reject([&] { session.apply(integrationInitial()); });
        reject([&] { session.boot(nullptr, table); });
        session.boot(assets, table);
        check(session.snapshot().phase == SessionPhase::Ready, "Boot did not produce ready state");
        reject([&] { session.boot(assets, table); });
        session.apply(integrationInitial());
        auto initial = session.snapshot();
        check(initial.phase == SessionPhase::Running && initial.entities.size() == 2 && initial.sceneGeneration == 1, "Initial scene did not commit");
        ++cases;
        session.advanceNanoseconds(100000000, integrationInput);
        auto moving = session.snapshot(); near(moving.entities[0].x, -117.6); near(moving.entities[1].x, 120);
        check(moving.hostTick == 6 && moving.worldTick == 6, "Fixed-step clock did not advance six times"); ++cases;
        auto unchanged = session.snapshot();
        TickInput bad; bad.scene = *integrationInitial().scene; bad.scene->entities.push_back(bad.scene->entities[0]);
        reject([&] { session.apply(bad); }); same(unchanged, session.snapshot());
        bad = {}; bad.scene = SceneSpec{"original_level", true, {}};
        reject([&] { session.apply(bad); }); same(unchanged, session.snapshot());
        bad = {}; bad.actors = {{{1, 1}, integrationIdle()}, {{1, 999}, integrationIdle()}};
        reject([&] { session.apply(bad); }); same(unchanged, session.snapshot());
        bad = {}; auto missingClip = integrationIdle(); missingClip.directMove = 7;
        bad.actors.push_back({{1, 1}, missingClip});
        reject([&] { session.apply(bad); }); same(unchanged, session.snapshot());
        reject([&] { session.advanceNanoseconds(NativeSession::MaximumAdvanceNs + 1, integrationInput); });
        same(unchanged, session.snapshot()); ++cases;

        NativeSession paused; paused.boot(assets, table); paused.apply(integrationInitial());
        for (int i = 0; i < 8; ++i) paused.advanceNanoseconds(250000000, integrationInput);
        const auto beforePause = paused.snapshot();
        for (int i = 0; i < 4; ++i) paused.advanceNanoseconds(250000000, integrationInput);
        const auto afterPause = paused.snapshot();
        check(afterPause.phase == SessionPhase::Paused && afterPause.hostTick == 180 && afterPause.worldTick == 120, "Pause advanced world time");
        sameEntities(beforePause, afterPause); // Includes an input change while paused.
        paused.advanceNanoseconds(16666667, integrationInput);
        auto resumed = paused.snapshot(); check(resumed.phase == SessionPhase::Running && resumed.worldTick == 121, "Resume did not update the world");
        near(resumed.entities[1].x, afterPause.entities[1].x - 0.3); ++cases;

        TickInput reload; reload.scene = SceneSpec{"next_native_scene", false, {{1, 3, 12, 7, 0, integrationIdle()}}};
        session.apply(reload); auto loaded = session.snapshot();
        check(loaded.sceneGeneration == 2 && loaded.worldTick == 0 && loaded.entities.size() == 1, "Scene replacement mixed old/new state");
        near(loaded.entities[0].y, 12); near(loaded.entities[0].render.bones[0][13], 12);
        bad = {}; bad.actors.push_back({{1, 1}, integrationIdle()});
        reject([&] { session.apply(bad); }); same(loaded, session.snapshot()); ++cases;

        TickInput remove; remove.remove.push_back({2, 1}); session.apply(remove);
        check(session.snapshot().entities.empty(), "Removed entity remains in snapshots");
        TickInput spawn; spawn.spawn.push_back({1, 0, 0, 0, 0, integrationIdle()});
        reject([&] { session.apply(spawn); }); // Never resurrect a stale slot ID.
        spawn.spawn[0].slot = 2; session.apply(spawn);
        check(session.snapshot().entities[0].handle == EntityHandle{2, 2}, "Spawn did not acquire a new handle"); ++cases;

        const auto at30 = runCadence(assets, table, 30), at60 = runCadence(assets, table, 60), at144 = runCadence(assets, table, 144);
        for (size_t i = 0; i < at30.size(); ++i) { same(at30[i], at60[i]); same(at30[i], at144[i]); }
        ++cases;
        for (auto service : {MissingService::OriginalLevel, MissingService::Collision, MissingService::OriginalPhysics,
                             MissingService::Audio, MissingService::SaveGame, MissingService::PhysicalInput})
            reject([&] { requireOriginalService(service); });
        session.stop(); session.stop();
        check(session.snapshot().phase == SessionPhase::Stopped && !session.snapshot().assets, "Stop did not release ownership");
        reject([&] { session.advanceNanoseconds(0, integrationInput); });
        reject([&] { session.apply(integrationInitial()); }); ++cases;

        NativeSession lifetime; auto owned = std::make_shared<AssetPackage>(*assets); std::weak_ptr<const AssetPackage> weak = owned;
        lifetime.boot(owned, table); lifetime.apply(integrationInitial()); auto retained = lifetime.snapshot(); owned.reset(); lifetime.stop();
        check(!weak.expired(), "Snapshot lost its resource lifetime"); retained.assets.reset();
        check(weak.expired(), "Session retained resource ownership after snapshots released"); ++cases;
        bool originalAssets = false;
        if (argc == 3) {
            auto actual = std::make_shared<const AssetPackage>(loadAssetPackage(argv[1]));
            auto actualTable = readJunoSelection(argv[2]);
            const auto actual30 = runCadence(actual, actualTable, 30), actual60 = runCadence(actual, actualTable, 60), actual144 = runCadence(actual, actualTable, 144);
            for (size_t i = 0; i < actual30.size(); ++i) { same(actual30[i], actual60[i]); same(actual30[i], actual144[i]); }
            originalAssets = true;
        }
        std::cout << "{\"status\":\"passed\",\"session_cases\":" << cases << ",\"rejected_cases\":" << rejected
                  << ",\"presentation_rates\":[30,60,144],\"cadence_checkpoints\":12,\"original_assets\":" << (originalAssets ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
