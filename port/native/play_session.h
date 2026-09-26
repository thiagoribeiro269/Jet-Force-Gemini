#pragma once
#include "movement_scenario.h"
#include "pad_recording.h"
#include <filesystem>
#include <functional>

namespace jfg_native {

// Playable Forest First: the converted private data, a native session with
// Juno at the original entry point, and one tick per controller read. Shared
// by the windowed executable and the headless replay, so a recording made
// while playing replays through exactly the same path.
struct PlayData {
    std::shared_ptr<const AssetPackage> character;
    JunoSelectionData selection;
    std::shared_ptr<const NativeRegion> region;
    std::shared_ptr<const JunoPhysicsData> physics;
    std::shared_ptr<const TrackCollision> collision;
    std::shared_ptr<const JunoCameraData> camera;
    uint64_t digest = 0;
};
inline const std::array<const char *, 7> &playFiles() {
    static const std::array<const char *, 7> files{"movement-scene.bin", "juno-selection.bin", "region-mesh.bin", "region-info.bin",
                                                   "collision.bin", "juno-physics.bin", "juno-camera.bin"};
    return files;
}
inline PlayData loadPlayData(const std::filesystem::path &folder) {
    std::array<std::string, 7> paths;
    PlayData data;
    data.digest = 1469598103934665603ull;
    for (size_t i = 0; i < paths.size(); ++i) {
        paths[i] = (folder / playFiles()[i]).string();
        std::ifstream input(paths[i], std::ios::binary);
        if (!input) throw std::runtime_error(std::string("Missing play data: ") + playFiles()[i]);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        data.digest = fnv1a(bytes, data.digest);
    }
    data.character = std::make_shared<const AssetPackage>(loadAssetPackage(paths[0].c_str()));
    data.selection = readJunoSelection(paths[1].c_str());
    data.region = loadRegion(paths[2].c_str(), paths[3].c_str());
    data.physics = readJunoPhysics(paths[5].c_str());
    data.collision = TrackCollision::load(paths[4].c_str(), data.physics->math);
    data.camera = readJunoCamera(paths[6].c_str());
    return data;
}

struct PlayFailure {
    uint64_t attempt = 0, hostTick = 0;
    bool notPorted = false;
    std::string technical, explanation;
};

class PlayRun {
    const PlayData *data_;
    NativeSession session_;
    uint8_t mode_;
    uint64_t attempts_ = 0;
    TickInput scene() const {
        auto input = movementScene(*data_->region);
        input.scene->entities.at(0).controlMode = mode_;
        return input;
    }
    void fail(const std::exception &error, bool notPorted, const std::string &explanation) {
        failures.push_back({attempts_, session_.hostTick(), notPorted, error.what(), explanation});
    }
public:
    std::vector<PlayFailure> failures;
    PlayRun(const PlayData &data, uint8_t mode) : data_(&data), mode_(mode) {
        if (mode > 1) throw std::runtime_error("Unknown original control mode");
        session_.boot(data.character, data.selection, data.region, data.physics, data.collision, data.camera);
        session_.apply(scene());
    }
    // The tick command for one record: optional restart (a new scene
    // generation), optional pause toggle, and Juno's controller state.
    TickInput input(const PadRecord &record) const {
        TickInput input;
        uint32_t generation = session_.sceneGeneration();
        bool paused = session_.phase() == SessionPhase::Paused;
        if (record.flags & RecordRestart) { input.scene = scene().scene; ++generation; paused = false; }
        if (record.flags & RecordPauseToggle) input.pause = !paused;
        ActorInput actor; actor.pad = record.pad;
        input.actors.push_back({EntityHandle{generation, 1}, actor});
        return input;
    }
    // One attempt of the next host tick (replay and tests).
    bool step(const PadRecord &record) {
        ++attempts_;
        try { session_.step(input(record)); return true; }
        catch (const NotPortedError &error) { fail(error, true, error.explanation()); }
        catch (const std::exception &error) { fail(error, false, "Erro interno do port."); }
        return false;
    }
    // Host time for the windowed executable: every tick asks the poll
    // callback for a record, which is also written to the recorder.
    bool advance(uint64_t nanoseconds, const std::function<PadRecord()> &poll, PadRecorder *recorder) {
        try {
            session_.advanceNanoseconds(nanoseconds, [&](uint64_t) {
                const auto record = poll();
                ++attempts_;
                if (recorder) recorder->append(record);
                return input(record);
            });
            return true;
        }
        catch (const NotPortedError &error) { fail(error, true, error.explanation()); }
        catch (const std::exception &error) { fail(error, false, "Erro interno do port."); }
        return false;
    }
    EntityHandle juno() const { return {session_.sceneGeneration(), 1}; }
    const JunoBody *body() const { return session_.body(juno()); }
    const JunoCamera *camera() const { return session_.camera(juno()); }
    bool paused() const { return session_.phase() == SessionPhase::Paused; }
    uint64_t attempts() const { return attempts_; }
    const NativeSession &session() const { return session_; }
    // Body, heading and camera position folded into one value.
    uint64_t stateDigest() const {
        uint64_t hash = 1469598103934665603ull;
        const auto *b = body();
        const auto *c = camera();
        if (!b || !c) return 0;
        uint32_t bits[7] = {};
        std::memcpy(bits, &b->position, 12); std::memcpy(bits + 3, &b->heading11C, 2); std::memcpy(bits + 4, &c->position, 12);
        for (auto word : bits) hash = (hash ^ word) * 1099511628211ull;
        return hash;
    }
};

// The movement proof script as a recording, for replay tests.
inline PadRecording scriptRecording(uint64_t digest) {
    PadRecording recording;
    recording.header.dataDigest = digest;
    for (uint64_t t = 0; t < MovementTicks; ++t) recording.records.push_back({movementPad(t), 0});
    return recording;
}
}
