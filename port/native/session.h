#pragma once
#include "scene_assets.h"
#include "planar_motion.h"
#include "juno_selection.h"
#include "renderer.h"
#include "region.h"
#include "juno_camera.h"
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
    std::optional<PadState> pad; // Original-body actors only: N64 controller state held from this tick on.
};
struct SpawnSpec {
    uint32_t slot; double x = 0, y = 0, z = 0, yaw = 0; ActorInput input;
    float scale = 1, visualOffsetY = 0;
    bool originalBody = false; // Recovered Juno movement and track collision.
    int16_t originalYaw = 0;
    uint8_t controlMode = 0;   // frontGetTargetControl menu option: 0 Normal, 1 Expert
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
        std::optional<JunoBody> body;
        std::optional<JunoCamera> camera;
        JoypadReader joypad;       // joyRead state of this actor's controller
        uint8_t controlMode = 0;
        Actor(EntityHandle id, const SpawnSpec &spawn, const AssetPackage &assets, const JunoSelectionData &selection,
              const std::shared_ptr<const JunoPhysicsData> &physics, const std::shared_ptr<const TrackCollision> &collision,
              const std::shared_ptr<const JunoCameraData> &cameraData)
            : handle(id), elevation(spawn.y), scale(spawn.scale), visualOffsetY(spawn.visualOffsetY),
              motion(spawn.x, spawn.z, spawn.yaw), animation(selection, assets.clips, assets.skeleton.size()), held(spawn.input) {
            if (!std::isfinite(elevation) || std::abs(elevation) > 1000000) throw std::runtime_error("Invalid entity elevation");
            if (!std::isfinite(scale) || scale <= 0 || scale > 1024 || !std::isfinite(visualOffsetY) || std::abs(visualOffsetY) > 8192)
                throw std::runtime_error("Invalid native model placement");
            if (spawn.originalBody) {
                if (!physics || !collision) requireOriginalService(MissingService::OriginalPhysics);
                if (spawn.controlMode > 1) throw std::runtime_error("Unknown original control mode");
                if (spawn.input.pad) validatePad(*spawn.input.pad);
                controlMode = spawn.controlMode;
                if (spawn.input.directMove || spawn.input.movement.x != 0 || spawn.input.movement.z != 0 || spawn.input.movement.low)
                    throw std::runtime_error("Original-body actors take controller input only");
                body.emplace(physics, collision, selection, assets.clips, Vec3f{float(spawn.x), float(spawn.y), float(spawn.z)}, spawn.originalYaw);
                if (cameraData) camera.emplace(cameraData, *body);
                followBody(0);
            } else {
                if (spawn.input.pad) throw std::runtime_error("Controller input requires an original-body actor");
                consume(held);
            }
        }
        void consume(const ActorInput &input) {
            auto next = motion; next.command(input.movement);
            if (input.directMove) animation.request(*input.directMove, input.selection, input.initialFraction, input.blendSeconds);
            else animation.motion(input.selection, input.initialFraction, input.blendSeconds);
            motion = next;
        }
        void followBody(double seconds) {
            const auto move = animation.selector().local(body->move3B);
            animation.follow(move, body->progress28, held.blendSeconds, seconds);
        }
        void setInput(const ActorInput &input) {
            if (body) {
                if (input.directMove || input.movement.x != 0 || input.movement.z != 0 || input.movement.low)
                    throw std::runtime_error("Original-body actors take controller input only");
                if (input.pad) validatePad(*input.pad);
                if (!std::isfinite(input.blendSeconds) || input.blendSeconds < 0 || input.blendSeconds > 10)
                    throw std::runtime_error("Invalid transition duration");
                held = input;
                return;
            }
            if (input.pad) throw std::runtime_error("Controller input requires an original-body actor");
            Actor validate(*this); validate.consume(input);
            held = input; // A paused actor retains its displayed state until resuming.
        }
        void advance(double seconds) {
            if (body) {
                // joyRead once per world tick; the controller keeps its last state.
                stepJuno(*body, camera ? &*camera : nullptr, joypad.read(held.pad.value_or(PadState{})), controlMode);
                followBody(seconds);
                return;
            }
            consume(held); motion.advance(seconds); animation.advance(seconds);
        }
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
    std::shared_ptr<const JunoPhysicsData> physics_;
    std::shared_ptr<const TrackCollision> collision_;
    std::shared_ptr<const JunoCameraData> camera_;
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
        if (spec.originalBody && !world.region) throw std::runtime_error("Original-body actor requires a region scene");
        world.actors.push_back(std::make_unique<Actor>(EntityHandle{world.generation, spec.slot}, spec, *assets_, selection_, physics_, collision_, camera_));
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
    void boot(std::shared_ptr<const AssetPackage> assets, const JunoSelectionData &selection, std::shared_ptr<const NativeRegion> region = {},
              std::shared_ptr<const JunoPhysicsData> physics = {}, std::shared_ptr<const TrackCollision> collision = {},
              std::shared_ptr<const JunoCameraData> camera = {}) {
        if (phase_ != SessionPhase::Cold || !assets || !assets->rigged || assets->skeleton.empty())
            throw std::runtime_error("Invalid or repeated native boot");
        JunoAnimationController validate(selection, assets->clips, assets->skeleton.size());
        composePose(assets->skeleton, validate.animation().current());
        if (region && (!region->mesh || !region->mesh->worldGeometry || region->mesh->rigged || region->triangles.empty()))
            throw std::runtime_error("Invalid native region binding");
        if (bool(physics) != bool(collision) || (collision && (!region || collision->level != region->level || collision->geometry != region->geometry)))
            throw std::runtime_error("Original movement requires matching physics and region collision");
        selection_ = selection; assets_ = std::move(assets); region_ = std::move(region);
        if (camera && (!collision || camera->level != collision->level)) throw std::runtime_error("Original camera requires its region collision");
        physics_ = std::move(physics); collision_ = std::move(collision); camera_ = std::move(camera); phase_ = SessionPhase::Ready;
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
    // Exactly one host tick with the given input, as one iteration of
    // advanceNanoseconds: a failure leaves the tick to be retried.
    void step(const TickInput &input) {
        if (phase_ == SessionPhase::Cold || phase_ == SessionPhase::Stopped) throw std::runtime_error("Native session is not booted");
        execute(input, true);
        ++hostTick_;
    }
    SessionPhase phase() const { return phase_; }
    uint32_t sceneGeneration() const { return world_ ? world_->generation : 0; }
    uint64_t hostTick() const { return hostTick_; }
    SessionSnapshot snapshot() const {
        SessionSnapshot result{phase_, hostTick_, world_ ? world_->updates : 0, generation_, world_ ? world_->name : "", assets_, {}, world_ ? world_->region : nullptr};
        if (world_) for (const auto &actor : world_->actors) {
            auto bones = composePose(assets_->skeleton, actor->animation.animation().current());
            auto world = actor->motion.world(); world[13] = float(actor->elevation);
            if (actor->visualOffsetY != 0) world[13] += actor->visualOffsetY;
            if (actor->body) world = originalWorld(*actor->body);
            for (auto &bone : bones) {
                if (actor->scale != 1) bone = multiply(bone, uniformScale(actor->scale));
                bone = multiply(bone, world);
            }
            if (actor->body) {
                const auto &b = *actor->body;
                result.entities.push_back({actor->handle, b.position.x, b.position.y, b.position.z, b.orientation[0] * (2 * double(Pi) / 65536),
                                           actor->animation.animation().frame(), actor->animation.animation().id(),
                                           actor->animation.selection().transitionProfile, {0, std::move(bones)}});
                continue;
            }
            result.entities.push_back({actor->handle, actor->motion.x(), actor->elevation, actor->motion.z(), actor->motion.yaw(),
                                       actor->animation.animation().frame(), actor->animation.animation().id(),
                                       actor->animation.selection().transitionProfile, {0, std::move(bones)}});
        }
        return result;
    }
    void stop() {
        world_.reset(); assets_.reset(); region_.reset(); physics_.reset(); collision_.reset(); camera_.reset();
        selection_ = {}; accumulator_ = 0; phase_ = SessionPhase::Stopped;
    }
    // Original object orientation (mathOneFloatRPY basis) and position.
    static Matrix originalWorld(const JunoBody &body) {
        const auto &math = body.data().math;
        const auto x = math.rotateRPY(body.orientation, {1, 0, 0}), y = math.rotateRPY(body.orientation, {0, 1, 0});
        const auto z = math.rotateRPY(body.orientation, {0, 0, 1});
        return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, body.position.x, body.position.y, body.position.z, 1};
    }
    const JunoCamera *camera(EntityHandle id) const {
        if (!world_) return nullptr;
        for (const auto &actor : world_->actors) if (actor->handle == id) return actor->camera ? &*actor->camera : nullptr;
        return nullptr;
    }
    const JunoBody *body(EntityHandle id) const {
        if (!world_) return nullptr;
        for (const auto &actor : world_->actors) if (actor->handle == id) return actor->body ? &*actor->body : nullptr;
        return nullptr;
    }
};
}
