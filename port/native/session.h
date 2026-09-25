#pragma once
#include "scene_assets.h"
#include "planar_motion.h"
#include "juno_selection.h"
#include "renderer.h"
#include "region.h"
#include <functional>
#include <optional>
#include <set>

namespace jfg_native {
enum class SessionPhase { Cold, Ready, Running, Paused, Stopped };
inline const char *phaseName(SessionPhase phase) {
    switch (phase) {
        case SessionPhase::Cold: return "cold";
        case SessionPhase::Ready: return "ready";
        case SessionPhase::Running: return "running";
        case SessionPhase::Paused: return "paused";
        case SessionPhase::Stopped: return "stopped";
    }
    throw std::runtime_error("Invalid native session phase");
}
enum class MissingService { OriginalLevel, Collision, OriginalPhysics, Audio, SaveGame, PhysicalInput };
inline void requireOriginalService(MissingService service) {
    const char *names[] = {"Original level loading", "World collision", "Original character physics", "Audio output", "Save game", "Physical input"};
    const auto index = size_t(service);
    throw std::runtime_error(std::string(index < 6 ? names[index] : "Unknown service") + " is not integrated");
}
struct EntityHandle {
    uint32_t scene = 0, slot = 0;
    bool operator==(const EntityHandle &other) const { return scene == other.scene && slot == other.slot; }
};
struct ActorInput {
    CharacterInput movement;
    JunoSelectionState selection;
    std::optional<uint32_t> directMove;
    double initialFraction = 0, blendSeconds = 0.2;
};
struct SpawnSpec {
    uint32_t slot; double x = 0, y = 0, z = 0, yaw = 0; ActorInput input;
    float scale = 1, visualOffsetY = 0;
};
struct SceneSpec {
    std::string name; bool originalLevel = false; std::vector<SpawnSpec> entities;
    std::optional<uint32_t> regionId = std::nullopt; // Imported geometry; originalLevel still requires full gameplay startup.
};
struct EntityInput { EntityHandle entity; ActorInput input; };
struct TickInput {
    std::optional<SceneSpec> scene;
    std::optional<bool> pause;
    std::vector<SpawnSpec> spawn;
    std::vector<EntityHandle> remove;
    std::vector<EntityInput> actors;
};
struct EntitySnapshot {
    EntityHandle handle;
    double x, y, z, yaw, animationFrame;
    uint32_t clip, transitionProfile;
    RenderInstance render;
};
struct SessionSnapshot {
    SessionPhase phase;
    uint64_t hostTick, worldTick;
    uint32_t sceneGeneration;
    std::string sceneName;
    std::shared_ptr<const AssetPackage> assets;
    std::vector<EntitySnapshot> entities;
    std::shared_ptr<const NativeRegion> region;
    std::vector<RenderInstance> renderInstances() const {
        std::vector<RenderInstance> result;
        if (region) result.push_back({1, {}});
        for (const auto &entity : entities) result.push_back(entity.render);
        return result;
    }
};

// A native host session. It replaces application orchestration, not N64 devices.
// Original-game services not yet integrated fail explicitly. Scene transactions
// and per-tick snapshots keep partially applied changes out of rendering.
class NativeSession {
    struct Actor {
        EntityHandle handle;
        double elevation;
        float scale, visualOffsetY;
        PlanarMotion motion;
        JunoAnimationController animation;
        ActorInput held;
        Actor(EntityHandle id, const SpawnSpec &spawn, const AssetPackage &assets, const JunoSelectionData &selection)
            : handle(id), elevation(spawn.y), scale(spawn.scale), visualOffsetY(spawn.visualOffsetY),
              motion(spawn.x, spawn.z, spawn.yaw), animation(selection, assets.clips, assets.skeleton.size()), held(spawn.input) {
            if (!std::isfinite(elevation) || std::abs(elevation) > 1000000) throw std::runtime_error("Invalid entity elevation");
            if (!std::isfinite(scale) || scale <= 0 || scale > 1024 || !std::isfinite(visualOffsetY) || std::abs(visualOffsetY) > 8192)
                throw std::runtime_error("Invalid native model placement");
            consume(held);
        }
        void consume(const ActorInput &input) {
            auto next = motion; next.command(input.movement);
            if (input.directMove) animation.request(*input.directMove, input.selection, input.initialFraction, input.blendSeconds);
            else animation.motion(input.selection, input.initialFraction, input.blendSeconds);
            motion = next;
        }
        void setInput(const ActorInput &input) {
            Actor validate(*this); validate.consume(input);
            held = input; // A paused actor retains its displayed state until resuming.
        }
        void advance(double seconds) { consume(held); motion.advance(seconds); animation.advance(seconds); }
    };
    struct World {
        std::string name;
        uint32_t generation = 0;
        uint64_t updates = 0;
        std::vector<std::unique_ptr<Actor>> actors;
        std::set<uint32_t> usedSlots;
        std::shared_ptr<const NativeRegion> region;
        World() = default;
        World(const World &other) : name(other.name), generation(other.generation), updates(other.updates), usedSlots(other.usedSlots), region(other.region) {
            for (const auto &actor : other.actors) actors.push_back(std::make_unique<Actor>(*actor));
        }
    };
    SessionPhase phase_ = SessionPhase::Cold;
    std::shared_ptr<const AssetPackage> assets_;
    std::shared_ptr<const NativeRegion> region_;
    JunoSelectionData selection_;
    std::unique_ptr<World> world_;
    uint64_t hostTick_ = 0, accumulator_ = 0;
    uint32_t generation_ = 0;

    static Actor &find(World &world, EntityHandle id) {
        for (auto &actor : world.actors) if (actor->handle == id) return *actor;
        throw std::runtime_error("Stale or absent native entity handle");
    }
    void spawn(World &world, const SpawnSpec &spec) const {
        if (!spec.slot || world.actors.size() >= 64 || world.usedSlots.count(spec.slot))
            throw std::runtime_error("Duplicate or unbounded native entity slot");
        world.actors.push_back(std::make_unique<Actor>(EntityHandle{world.generation, spec.slot}, spec, *assets_, selection_));
        world.usedSlots.insert(spec.slot);
    }
    void execute(const TickInput &input, bool update) {
        if (phase_ == SessionPhase::Cold || phase_ == SessionPhase::Stopped) throw std::runtime_error("Native session is not booted");
        auto next = world_ ? std::make_unique<World>(*world_) : nullptr;
        auto nextGeneration = generation_;
        bool paused = phase_ == SessionPhase::Paused;
        if (input.scene) {
            if (input.scene->originalLevel) requireOriginalService(MissingService::OriginalLevel);
            if (input.scene->name.empty() || input.scene->name.size() > 128 || generation_ == UINT32_MAX)
                throw std::runtime_error("Invalid native scene definition");
            next = std::make_unique<World>(); next->name = input.scene->name; next->generation = ++nextGeneration;
            if (input.scene->regionId) {
                if (!region_ || region_->level != *input.scene->regionId) throw std::runtime_error("Scene references an unloaded native region");
                next->region = region_;
            }
            for (const auto &entity : input.scene->entities) spawn(*next, entity);
            paused = false;
        }
        if (!next && (input.pause || !input.spawn.empty() || !input.remove.empty() || !input.actors.empty()))
            throw std::runtime_error("Entity or pause command requires a loaded scene");
        if (next) {
            // IDs cannot be removed and reused within a scene: stale references
            // remain invalid until a new scene generation gives them a new ID.
            for (auto id : input.remove) {
                find(*next, id);
                next->actors.erase(std::remove_if(next->actors.begin(), next->actors.end(),
                                   [&](const auto &actor) { return actor->handle == id; }), next->actors.end());
            }
            for (const auto &entity : input.spawn) spawn(*next, entity);
            std::set<uint32_t> touched;
            for (const auto &command : input.actors) {
                if (!touched.insert(command.entity.slot).second) throw std::runtime_error("Duplicate actor input in one tick");
                find(*next, command.entity).setInput(command.input);
            }
            if (input.pause) paused = *input.pause;
            if (update && !paused) {
                for (auto &actor : next->actors) actor->advance(1.0 / TickRate);
                ++next->updates;
            }
        }
        world_ = std::move(next); generation_ = nextGeneration;
        phase_ = !world_ ? SessionPhase::Ready : paused ? SessionPhase::Paused : SessionPhase::Running;
    }
public:
    static constexpr uint64_t TickRate = 60, ClockScale = 1000000000, MaximumAdvanceNs = 250000000;
    NativeSession() = default;
    NativeSession(const NativeSession &) = delete;
    NativeSession &operator=(const NativeSession &) = delete;
    void boot(std::shared_ptr<const AssetPackage> assets, const JunoSelectionData &selection, std::shared_ptr<const NativeRegion> region = {}) {
        if (phase_ != SessionPhase::Cold || !assets || !assets->rigged || assets->skeleton.empty())
            throw std::runtime_error("Invalid or repeated native boot");
        JunoAnimationController validate(selection, assets->clips, assets->skeleton.size());
        composePose(assets->skeleton, validate.animation().current());
        if (region && (!region->mesh || !region->mesh->worldGeometry || region->mesh->rigged || region->triangles.empty()))
            throw std::runtime_error("Invalid native region binding");
        selection_ = selection; assets_ = std::move(assets); region_ = std::move(region); phase_ = SessionPhase::Ready;
    }
    void apply(const TickInput &input) { execute(input, false); }
    void advanceNanoseconds(uint64_t elapsed, const std::function<TickInput(uint64_t)> &source) {
        if (phase_ == SessionPhase::Cold || phase_ == SessionPhase::Stopped || elapsed > MaximumAdvanceNs)
            throw std::runtime_error("Invalid native host time step");
        // Rational integer accumulator: exactly 60 steps per second, regardless
        // of presentation cadence. No rounded 16,666,667 ns fixed-step constant.
        if (accumulator_ > ClockScale * 16) throw std::runtime_error("Unresolved native tick error backlog");
        accumulator_ += elapsed * TickRate;
        while (accumulator_ >= ClockScale) {
            execute(source(hostTick_), true);
            accumulator_ -= ClockScale; ++hostTick_;
        }
    }
    SessionSnapshot snapshot() const {
        SessionSnapshot result{phase_, hostTick_, world_ ? world_->updates : 0, generation_, world_ ? world_->name : "", assets_, {}, world_ ? world_->region : nullptr};
        if (world_) for (const auto &actor : world_->actors) {
            auto bones = composePose(assets_->skeleton, actor->animation.animation().current());
            auto world = actor->motion.world(); world[13] = float(actor->elevation);
            if (actor->visualOffsetY != 0) world[13] += actor->visualOffsetY;
            for (auto &bone : bones) {
                if (actor->scale != 1) bone = multiply(bone, uniformScale(actor->scale));
                bone = multiply(bone, world);
            }
            result.entities.push_back({actor->handle, actor->motion.x(), actor->elevation, actor->motion.z(), actor->motion.yaw(),
                                       actor->animation.animation().frame(), actor->animation.animation().id(),
                                       actor->animation.selection().transitionProfile, {0, std::move(bones)}});
        }
        return result;
    }
    void stop() {
        world_.reset(); assets_.reset(); region_.reset(); selection_ = {}; accumulator_ = 0; phase_ = SessionPhase::Stopped;
    }
};
}
