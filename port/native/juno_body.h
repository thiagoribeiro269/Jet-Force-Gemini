#pragma once
#include "track_collision.h"
#include "juno_selection.h"
#include <optional>

namespace jfg_native {

// Juno movement recovered from the original player code and ported to PC
// structures. Sources (static reading, bytes audited by movement_assets.py):
// objObjectsTick (previous position), boyControl (overlay 16), its walking
// state 0x2708, air state 0x3464, lateral decay 0x4934, move machine 0x5120,
// movement 0x5BB8, controlPlatform, controlMakeGravity, controlHalfTurn,
// controlWalkingBack, controlGroundHits, func_80035628, func_800344C8,
// objMoveXYZ, objAnimDframe/objAnimSetMove and mathRnd. One call is one
// original 1/60-second frame (frames = 1). Fields keep original offsets.
struct JunoSphere { Vec3f offset; float radius = 0; uint8_t flags = 0, rotate = 0; };
struct JunoPhysicsData {
    std::array<JunoSphere, 5> spheres{};
    std::array<uint8_t, 4> masks{};  // +0x531 skip, feet (+5), +6, +7 of the character table
    uint32_t exclude = 0, include = 0;
    std::array<float, 4> gravityCharacter{};
    std::array<float, 3> gravityState{};
    float turnAimRate = 0, turnAimTarget = 0, turnRate = 0, turnTarget = 0, turnFloor = 0, halfTurnFactor = 0, speedRate = 0,
          brakeTrigger = 0, leanFactor = 0, lateralDecay = 0, lateralZeroLow = 0, lateralZeroHigh = 0, slopeBias = 0,
          slopeLimit = 0, lockedDamping = 0, jumpAnimFloor = 0, airSpeedScale = 0, airSpeedBase = 0, airTurnScale = 0, airTurnBase = 0;
    uint32_t rngSeed = 0;
    std::array<float, 52> rates{};
    std::array<float, 14> thresholds{};  // data +0xA44..+0xA78
    OriginalMath math;
};

inline std::shared_ptr<const JunoPhysicsData> readJunoPhysics(const char *path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    size_t at = 0;
    auto take = [&](size_t count) { if (at + count > bytes.size()) throw std::runtime_error("Truncated Juno physics data");
                                     const auto *p = bytes.data() + at; at += count; return p; };
    auto u32 = [&]() { const auto *p = take(4); return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    auto f32 = [&]() {
        const auto bits = u32(); float value; std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value)) throw std::runtime_error("Nonfinite Juno physics value");
        return value;
    };
    if (bytes.size() < 16 || std::memcmp(take(8), "JFGPHY1\0", 8)) throw std::runtime_error("Wrong Juno physics format");
    if (u32() != 5 || u32() != 20) throw std::runtime_error("Unsupported Juno physics profile");
    auto data = std::make_shared<JunoPhysicsData>();
    for (auto &sphere : data->spheres) {
        sphere.offset = {f32(), f32(), f32()}; sphere.radius = f32();
        sphere.flags = *take(1); sphere.rotate = *take(1);
        if (take(2)[0] | bytes[at - 1]) throw std::runtime_error("Nonzero Juno sphere padding");
        if (!(sphere.radius > 0) || sphere.radius > 64) throw std::runtime_error("Invalid Juno sphere radius");
    }
    std::memcpy(data->masks.data(), take(4), 4);
    data->exclude = u32(); data->include = u32();
    for (auto &value : data->gravityCharacter) value = f32();
    for (auto &value : data->gravityState) value = f32();
    for (float *value : {&data->turnAimRate, &data->turnAimTarget, &data->turnRate, &data->turnTarget, &data->turnFloor,
                         &data->halfTurnFactor, &data->speedRate, &data->brakeTrigger, &data->leanFactor, &data->lateralDecay,
                         &data->lateralZeroLow, &data->lateralZeroHigh, &data->slopeBias, &data->slopeLimit, &data->lockedDamping,
                         &data->jumpAnimFloor, &data->airSpeedScale, &data->airSpeedBase, &data->airTurnScale, &data->airTurnBase})
        *value = f32();
    data->rngSeed = u32();
    for (auto &value : data->rates) value = f32();
    for (auto &value : data->thresholds) value = f32();
    std::array<float, 1025> sine{};
    std::array<int16_t, 1025> arctan{};
    for (auto &value : sine) value = f32();
    for (auto &value : arctan) { const auto *p = take(2); value = int16_t(p[0] | p[1] << 8); }
    while (at % 4) if (*take(1)) throw std::runtime_error("Nonzero Juno physics padding");
    if (at != bytes.size()) throw std::runtime_error("Trailing Juno physics payload");
    data->math = OriginalMath(sine, arctan);
    if (data->exclude != 0xCE002000u || data->include != 0x02000000u || data->masks != std::array<uint8_t, 4>{0x18, 0x01, 0x08, 0x16} ||
        data->gravityCharacter[0] != 0.45f || data->gravityState[0] != 1.0f || data->speedRate != 0.95f || data->rates[0] != 0.015f)
        throw std::runtime_error("Juno physics values differ from the inspected profile");
    return data;
}

// Raw controller values (OSContPad stick_x/stick_y, signed 8-bit), the jump
// button held/pressed this frame, and the active camera yaw (*controlcam).
struct JunoControl {
    int32_t stickX = 0, stickY = 0;
    bool jumpHeld = false, jumpPressed = false;
    bool cRight = false, cLeft = false, trigger = false;  // camera and aim buttons (controlKeys 0x1, 0x2, 0x10)
    int16_t cameraYaw = 0;                                // *controlcam
};

// controller.c joyClamp: dead zone 5, offset by 5, range 65.
inline int32_t joyClamp(int32_t raw) {
    auto magnitude = int8_t(raw);
    if (magnitude < 5 && magnitude > -5) return 0;
    if (magnitude > 0) { magnitude = int8_t(magnitude - 5); if (magnitude > 65) magnitude = 65; }
    else { magnitude = int8_t(magnitude + 5); if (magnitude < -65) magnitude = -65; }
    return magnitude;
}

// mathRnd: 64-bit shift generator on a 32-bit seed (DKR lineage).
class OriginalRandom {
    uint32_t seed_;
public:
    explicit OriginalRandom(uint32_t seed) : seed_(seed) {}
    int32_t next(int32_t low, int32_t high) {
        const uint64_t s = uint64_t(int64_t(int32_t(seed_)));
        uint64_t mixed = ((s << 63) >> 31) | ((s << 31) >> 32);
        mixed ^= (s << 44) >> 32;
        const uint64_t result = ((mixed >> 20) & 0xFFF) ^ mixed;
        seed_ = uint32_t(result);
        const uint32_t range = uint32_t(high - low) + 1;
        return int32_t((uint32_t(result) - uint32_t(low)) % range) + low;
    }
    uint32_t seed() const { return seed_; }
};

class JunoBody {
public:
    // Object fields.
    std::array<int16_t, 3> orientation{};   // +0x00 yaw, +0x02 pitch, +0x04 roll
    Vec3f position, velocity;                // +0x0C, +0x1C
    // Player fields by offset.
    float speed04 = 0, local08 = 0, lateral10 = 0, turnBlend14 = 0.075f;
    Vec3f previous3C, safe524;
    float down50 = 0, down54 = 0, down58 = 0, verticalSpeed74 = 0, hover594 = 0;
    int16_t heading11C = 0, lean11E = 0, lean120 = 0, heading128 = 0, halfTurnHeading13C = 0;
    uint8_t halfTurn13E = 0, halfTurnSkid13F = 0, state568 = 0, previousState56A = 0, walkingBack569 = 0, hover14A = 0;
    int8_t push149 = 0, airborne185 = 0, jumpDelay57A = 0, landingLock56B = 0, skid576 = 0;
    uint8_t jumpCharge57B = 0, jumpReleased58E = 0, groundFlag184 = 1, blocked199 = 0;
    float push6C = 0, push70 = 0, fallStart57C = 0;
    uint8_t floor532 = 0, wall533 = 0, ceiling534 = 0, skip531 = 0;
    uint32_t surfaceFlags520 = 0;
    std::array<Vec3f, 5> sphereBase364, sphereCurrent3C4, spherePrevious484;
    // Object animation fields driven by the move machine.
    uint32_t move3B = 16;                    // +0x3B
    float progress28 = 0;                    // +0x28, normalized clip position
    float rate = 0;                          // last objAnimDframe rate
    uint32_t transitionProfile = 0;
    uint32_t moveChanges = 0;
    float stickSpeed = 0;
    uint32_t lastCollisionMask = 0, frames = 0;

    JunoBody(std::shared_ptr<const JunoPhysicsData> data, std::shared_ptr<const TrackCollision> track, const JunoSelectionData &selection,
             const std::vector<Clip> &clips, Vec3f spawn, int16_t yaw)
        : data_(std::move(data)), query_(track), selector_(selection), clips_(&clips), random_(data_ ? data_->rngSeed : 0) {
        if (!data_) throw std::runtime_error("Missing Juno physics data");
        for (float value : {spawn.x, spawn.y, spawn.z})
            if (!std::isfinite(value) || std::abs(value) > 100000) throw std::runtime_error("Invalid Juno spawn");
        // controlPlayerInit subset for character type 0.
        orientation = {yaw, 0, 0};
        position = spawn;
        heading11C = heading128 = yaw;
        fallStart57C = spawn.y;
        skip531 = data_->masks[0];
        for (size_t i = 0; i < 5; ++i) sphereBase364[i] = data_->spheres[i].offset;
        placeSpheres();
        spherePrevious484 = sphereCurrent3C4;
        previous3C = safe524 = position;
        requestMove(16, 0.0f); // objAnimSetMove(arg0, 0x10, 0) for types 0/1.
    }
    const TrackQuery &query() const { return query_; }
    // objMoveXYZ(player, dx, 0, dz) issued by the free camera (func_8002CF78).
    void pushByCamera(float dx, float dz) { objMove({dx, 0.0f, dz}); }
    const JunoPhysicsData &data() const { return *data_; }
    uint32_t clipId() const { return selector_.resolve(move3B, {}).clip; }
    bool grounded() const { return data_->masks[1] & floor532; }

    // One original frame for player type 0.
    void tick(const JunoControl &control) {
        for (int32_t v : {control.stickX, control.stickY})
            if (v < -128 || v > 127) throw std::runtime_error("Stick value outside the controller range");
        const auto &math = data_->math;
        constexpr int32_t frames = 1;
        constexpr float dt = 1.0f;
        ++this->frames;
        previous3C = position;                  // objObjectsTick
        bool disabled = false;                  // disablejoy, cleared by controlPlayer each frame
        safe524 = previous3C;                   // controlPlatform without a platform object
        lean11E = 0; lean120 = 0;
        // boyControl: legitimate-ROM joystick scale; unk189 variants inactive.
        const float scale = 0.0625f;
        if (landingLock56B) {
            disabled = true;
            landingLock56B = int8_t(landingLock56B - frames);
            if (landingLock56B < 0) landingLock56B = 0;
        }
        // controlReadJoypad: joyGetStickX/Y apply joyClamp to the raw pad values.
        const int32_t stickX = disabled ? 0 : joyClamp(control.stickX), stickY = disabled ? 0 : joyClamp(control.stickY);
        const bool jumpHeld = !disabled && control.jumpHeld, jumpPressed = !disabled && control.jumpPressed;
        // controlMakeGravity: table1[type & 3] * table2[+0x575]; +0x194 is 0.
        const float gravity = data_->gravityCharacter[0] * data_->gravityState[0];
        clamp50(velocity.x); clamp50(velocity.y); clamp50(velocity.z); clamp50(speed04); clamp50(local08);
        const auto down = math.rotateYPR({int16_t(-heading11C), int16_t(-orientation[1]), int16_t(-orientation[2])}, {0.0f, -1.0f, 0.0f});
        down54 = down.x; down58 = down.y; down50 = down.z;
        float magnitude = std::sqrt(float((stickX * stickX) + (stickY * stickY)));
        if (magnitude > 65.0f) magnitude = 65.0f;
        float speed = magnitude * scale;
        stickSpeed = speed;
        int16_t direction = speed != 0.0f ? int16_t(int32_t(math.arctanf(float(stickX), float(-stickY))) - control.cameraYaw) : heading11C;
        const uint8_t feet = data_->masks[1];
        if ((state568 == 0 || state568 == 5 || state568 == 11) && !(feet & floor532)) becomeAirborne(frames);
        else if ((state568 == 2 || state568 == 1) && !(feet & floor532) && !(data_->masks[2] & floor532) && !(data_->masks[3] & floor532))
            becomeAirborne(frames);
        else airborne185 = 0;
        if (((feet & floor532) && state568 != 3) || state568 == 4) fallStart57C = position.y;
        // Water and lava from trackPolyHeight(x, z, +0x5C, 0x8000A000): not ported.
        float surfaceHeight = 0;
        if (query_.polyHeight(position.x, position.z, surfaceHeight, 0x8000A000u))
            throw std::runtime_error("Water or lava surfaces are not ported");
        if (state568 != 0 && state568 != 2) {
            halfTurn13E = 0;
            if (state568 != 1) skid576 = 0;
        }
        if (state568 == 0) walk(speed, direction, frames, jumpHeld, jumpPressed, stickX, stickY, scale, !disabled && control.trigger);
        else if (state568 == 3) air(speed, direction, frames, jumpHeld);
        else throw std::runtime_error("Juno state outside the ported walking/air subset");
        animate(dt);
        move(gravity, frames, dt, disabled);
        // controlPlayer after the character routine: push decay.
        if (push149) {
            const float decay = OriginalMath::powerf(0.9f, frames);
            push6C = push6C * decay;
            push70 = push70 * decay;
        }
    }

private:
    std::shared_ptr<const JunoPhysicsData> data_;
    TrackQuery query_;
    JunoSelector selector_;
    const std::vector<Clip> *clips_;
    OriginalRandom random_;
    int32_t forcedMove_ = -1;

    static void clamp50(float &value) {
        if (value < -50.0f) value = -50.0f;
        if (value > 50.0f) value = 50.0f;
    }
    static float magnitude(float value) { return value < 0.0f ? -value : value; }
    JunoSelectionState selectionState() const {
        JunoSelectionState state;
        state.component04 = speed04; state.component10 = lateral10;
        state.walkingBack569 = walkingBack569;
        return state;
    }
    // func_overlay_16_01004E08 with mathRnd(16,19) drawn from the original generator.
    uint32_t chooseMove() {
        auto state = selectionState();
        const float a = magnitude(speed04), b = magnitude(lateral10);
        if (std::max(a, b) < 0.1f && !state.flag1FA && !state.flag1F9 && !state.flag1F4 && !state.flag198)
            state.idleDraw = uint32_t(random_.next(16, 19));
        return selector_.choose(state);
    }
    // func_overlay_16_01004F78: remap, then objAnimSetMove only on change.
    void requestMove(uint32_t requested, float fraction) {
        const auto selected = selector_.resolve(requested, {});
        if (forcedMove_ != -1 || selected.local != move3B) {
            if (fraction > 1.0f) fraction = 1.0f;
            else if (fraction < 0.0f) fraction = 0.0f;
            move3B = selected.local;
            progress28 = fraction;
            ++moveChanges;
        }
        forcedMove_ = -1;
        transitionProfile = selected.transitionProfile;
    }
    bool clipLoops() const {
        const auto id = clipId();
        for (const auto &clip : *clips_) if (clip.id == id) return clip.loop;
        throw std::runtime_error("Juno move without a converted clip");
    }
    void becomeAirborne(int32_t frames) {
        airborne185 = int8_t(airborne185 + frames);
        if (airborne185 >= 0x10) {
            fallStart57C = position.y;
            requestMove(0x18, 0.0f);
            previousState56A = state568;
            state568 = 3;
            jumpReleased58E = 0;
        }
    }
    // func_80035628: sphere centres from base offsets, orientation and position.
    void placeSpheres() {
        for (size_t i = 0; i < 5; ++i) {
            Vec3f p = sphereBase364[i];
            if (data_->spheres[i].rotate) p = data_->math.rotateRPY(orientation, p);
            sphereCurrent3C4[i] = {p.x + position.x, p.y + position.y, p.z + position.z};
        }
    }
    // controlHalfTurn(obj, player, &speed, &direction, 0x6AAA, 0x71C, &skid, scale).
    void halfTurn(float &speed, int16_t &direction, bool jumpPressed, int32_t stickX, int32_t stickY, float scale, bool &skid) {
        const auto &math = data_->math;
        constexpr int16_t wide = 0x6AAA, narrow = 0x71C;
        skid = false;
        if (jumpPressed || walkingBack569 || (state568 != 0 && state568 != 2)) { halfTurn13E = 0; return; }
        if (!halfTurn13E) {
            const auto difference = int16_t(orientation[0] - direction);
            if (difference < -wide || wide < difference) {
                halfTurnHeading13C = orientation[0];
                halfTurn13E = 1; halfTurnSkid13F = 0;
                if (speed04 < -2.5f) { skid576 = 2; halfTurnSkid13F = 1; skid = true; }
            }
            return;
        }
        if (halfTurnSkid13F) { skid576 = 2; speed = 0.0f; }
        else {
            const float s = -math.sinf(halfTurnHeading13C), c = -math.cosf(halfTurnHeading13C);
            const float sy = math.sinf(orientation[0]), cy = math.cosf(orientation[0]);
            const float x = float(stickX), y = float(-stickY);
            const float a = s * ((x * cy) + (y * sy)), b = c * ((y * cy) - (x * sy));
            speed = std::sqrt((a * a) + (b * b)) * scale;
        }
        const auto back = int16_t(halfTurnHeading13C - direction);
        if (-wide < back && back < wide) halfTurn13E = 0;
        const auto turned = int16_t(direction - orientation[0]);
        if (-narrow < turned && turned < narrow) halfTurn13E = 0;
        if (!halfTurn13E) {
            if (speed > 0.0f) speed = -speed;
            if (speed04 > 0.0f) speed04 = -speed04;
            skid576 = 0;
        }
    }
    // func_overlay_16_01004934 without strafe or roll buttons: lateral decay.
    void lateralDecay(int32_t frames) {
        lateral10 = lateral10 * OriginalMath::powerf(data_->lateralDecay, frames);
        if (data_->lateralZeroLow < lateral10 && lateral10 < data_->lateralZeroHigh) lateral10 = 0.0f;
    }
    // Walking state 0: func_overlay_16_01002708.
    void walk(float speed, int16_t direction, int32_t frames, bool jumpHeld, bool jumpPressed, int32_t stickX, int32_t stickY, float scale,
              bool trigger) {
        (void)jumpHeld;
        const auto &math = data_->math;
        const auto &d = *data_;
        if (skid576) {
            skid576 = int8_t(skid576 - frames);
            if (skid576 < 0) skid576 = 0;
        }
        if (move3B == 0x19 && landingLock56B == 0) requestMove(chooseMove(), 0.0f);
        if (trigger) throw std::runtime_error("Aim state 0xB (controlKeys 0x10) is not ported");
        if (5.0f < speed) speed = 5.0f;  // controlWalkingBack, not aiming
        lateralDecay(frames);
        if (jumpPressed && ceiling534 == 0) {
            const float current = magnitude(speed04);
            if (walkingBack569) { jumpDelay57A = 7; jumpCharge57B = 0; requestMove(7, 0.0f); }
            else if (current > 1.25f && speed != 0.0f) {
                jumpDelay57A = 0;
                velocity.y = velocity.y + 10.0f;
                requestMove(6, 0.0f);
            } else {
                jumpDelay57A = 0x18; jumpCharge57B = 0;
                requestMove(5, 0.0f);
            }
            state568 = 3; jumpReleased58E = 0; fallStart57C = position.y;
        }
        if (!walkingBack569) speed = -speed; else direction = int16_t(direction + 0x8000);
        bool skid = false;
        halfTurn(speed, direction, jumpPressed, stickX, stickY, scale, skid);
        if (skid) requestMove(0xF, 0.0f);
        const int16_t before = heading11C;
        turnBlend14 = turnBlend14 + ((d.turnTarget - turnBlend14) * (1.0f - OriginalMath::powerf(d.turnRate, frames)));
        float speedSquared = magnitude(speed04) * 1.0f;
        speedSquared = speedSquared * speedSquared;
        if (speedSquared > 1.0f) speedSquared = 1.0f;
        float keep = 1.0f - (((turnBlend14 - d.turnFloor) * speedSquared) + d.turnFloor);
        if (halfTurn13E) keep = d.halfTurnFactor;
        heading11C = OriginalMath::dAngle(heading11C, direction, 1.0f - OriginalMath::powerf(keep, frames));
        const float old = speed04;
        speed04 = ((1.0f - OriginalMath::powerf(d.speedRate, frames)) * (speed - speed04)) + speed04;
        const auto turned = int16_t(heading11C - before);
        if (state568 == 0 && (old - speed04) < d.brakeTrigger && move3B != 0x15 && move3B != 0x2E && move3B != 0x19) {
            skid576 = 4;
            requestMove(0xF, 0.0f);
        }
        float lean = magnitude(speed04);
        if (lean > 3.0f) lean = 3.0f;
        lean120 = int16_t(OriginalMath::truncate((float(turned) * lean) * d.leanFactor));
        (void)math;
    }
    // Air state 3: func_overlay_16_01003464 without hover or sidekick branches.
    void air(float speed, int16_t direction, int32_t frames, bool jumpHeld) {
        const auto &d = *data_;
        skid576 = 0;
        if (!jumpHeld) jumpReleased58E = 1;
        if (grounded() && jumpDelay57A == 0) {
            hover594 = 0.0f;
            state568 = previousState56A;
            const float drop = fallStart57C - position.y;
            if (drop > 120.0f) {
                requestMove(0x19, 0.0f);
                landingLock56B = 0x1E;
                state568 = 0;
            } else if (state568 == 2) requestMove(4, 0.0f);
            else if (state568 == 1) requestMove(0xD, 0.0f);
            else { requestMove(chooseMove(), 0.0f); state568 = 0; }
        } else if (jumpDelay57A > 0) {
            jumpDelay57A = int8_t(jumpDelay57A - frames);
            if (jumpHeld) jumpCharge57B = uint8_t(jumpCharge57B + frames);
            else jumpDelay57A = 0;
            if (jumpDelay57A <= 0) {
                if (progress28 < 0.25f) progress28 = d.jumpAnimFloor;
                jumpDelay57A = 0;
                if (walkingBack569) velocity.y = velocity.y + 10.0f;
                else {
                    uint8_t charge = jumpCharge57B;
                    if (charge >= 0x19) { jumpCharge57B = 0x18; charge = 0x18; }
                    velocity.y = velocity.y + (6.0f + (((12.0f - 6.0f) * float(charge)) / 24.0f));
                }
            }
            speed = 0.0f;
        } else if (position.y < (fallStart57C - 120.0f) && move3B != 0x18) {
            requestMove(0x18, 0.0f);
        }
        if (5.0f < speed) speed = 5.0f;  // controlWalkingBack
        if (!walkingBack569) speed = -speed; else direction = int16_t(direction + 0x8000);
        speed04 = ((1.0f - OriginalMath::powerf(1.0f - ((hover594 * d.airSpeedScale) + d.airSpeedBase), frames)) * (speed - speed04)) + speed04;
        heading11C = OriginalMath::dAngle(heading11C, direction,
                                          1.0f - OriginalMath::powerf(1.0f - ((hover594 * d.airTurnScale) + d.airTurnBase), frames));
    }
    // func_overlay_16_01005120 for the converted move subset, then objAnimDframe.
    void animate(float dt) {
        const auto &t = data_->thresholds;  // t[0] = +0xA44, ...
        const float forward = magnitude(speed04), side = magnitude(lateral10);
        const float largest = forward < side ? side : forward;
        uint32_t next = move3B;
        float start = progress28;
        rate = data_->rates.at(move3B);
        auto idle = [&]() { next = uint32_t(random_.next(0x10, 0x13)); start = 0.0f; };
        auto strafe = [&]() { next = lateral10 < 0.0f ? 9 : 0xA; start = 0.25f; };
        switch (move3B) {
            case 0: case 28: case 36:
                rate = rate * largest;
                if (walkingBack569) next = 3;
                else if (largest < t[0]) idle();
                else if (forward < side) strafe();
                else if (largest > 1.75f) next = 1;
                break;
            case 1: case 29: case 35:
                rate = rate * largest;
                if (forward < side) strafe();
                else if (largest < 1.75f) next = 0;
                else if (largest > 3.5f) next = 2;
                break;
            case 2: case 30: case 34:
                rate = rate * largest;
                if (forward < side) strafe();
                else if (largest < 3.5f) next = 1;
                break;
            case 3: case 26:
                rate = rate * largest;
                if (largest < t[1] && stickSpeed <= speed04) idle();
                else if (forward < side) strafe();
                else if (!walkingBack569) next = 0;
                break;
            case 5:
                if ((progress28 + (rate * dt)) > 0.75f) { progress28 = 0.75f; rate = 0.0f; }
                break;
            case 9: case 10: case 31: case 32: case 47: case 48:
                rate = rate * side;
                if (largest < t[3]) idle();
                else if (side < forward) {
                    if (walkingBack569) { start = 0.25f; next = 3; }
                    else if (forward < 1.75f) { start = 0.25f; next = 0; }
                    else { next = 2; start = 0.25f; if (forward < 3.5f) next = 1; }
                }
                break;
            case 15:
                if (skid576 == 0) { next = chooseMove(); start = 0.0f; }
                break;
            case 16: case 17: case 18: case 19: case 20: case 27: case 45:
                if ((progress28 + (rate * dt)) > 1.0f) idle();
                if (t[8] < largest) { next = 3; start = 0.25f; if (!walkingBack569) next = 0; }
                break;
            case 6: case 7: case 24: case 25:
                break;
            default:
                throw std::runtime_error("Juno move outside the ported move-machine subset");
        }
        // objAnimDframe: advance the normalized clip position; loops wrap.
        progress28 = progress28 + (rate * dt);
        if (progress28 >= 1.0f) {
            if (clipLoops()) while (progress28 >= 1.0f) progress28 = progress28 - 1.0f;
            else progress28 = 1.0f;
        } else if (progress28 < 0.0f) {
            if (clipLoops()) while (progress28 < 0.0f) progress28 = progress28 + 1.0f;
            else progress28 = 0.0f;
        }
        requestMove(next, start);
    }
    // func_overlay_16_01005BB8 without platform, sidekick or transformer paths.
    void move(float gravity, int32_t frames, float dt, bool disabled) {
        const auto &math = data_->math;
        const auto &d = *data_;
        velocity.y = velocity.y - ((1.0f - hover594) * (gravity * dt));
        verticalSpeed74 = velocity.y;
        if (state568 != 2 && state568 != 10) {
            const float slope = down50;
            float bias = 0.0f;
            if (slope < 0.0f) {
                float amount = -slope - d.slopeBias;
                if (amount < 0.0f) amount = 0.0f;
                if (d.slopeLimit < amount) amount = d.slopeLimit;
                bias = amount * 35.0f;
            }
            speed04 = speed04 + ((gravity * (slope / (4.0f - bias))) * dt);
        }
        local08 = 0.0f;
        orientation[0] = heading11C;
        int16_t travel = (state568 == 4 || (state568 == 3 && hover14A)) ? heading128 : heading11C;
        if (halfTurn13E) travel = halfTurnHeading13C;
        const float s = math.sinf(travel), c = math.cosf(travel);
        if (state568 != 12) {
            velocity.x = speed04 * s;
            velocity.z = speed04 * c;
        }
        const int16_t side = halfTurn13E ? halfTurnHeading13C : heading11C;
        velocity.x = velocity.x + (lateral10 * math.cosf(side));
        velocity.z = velocity.z - (lateral10 * math.sinf(side));
        float mx = velocity.x, mz = velocity.z;
        if (push149) { mx = mx + push6C; mz = mz + push70; }
        if (disabled) {
            mx = mx * d.lockedDamping;
            mz = mz * d.lockedDamping;
            if (speed04 < -0.5f || speed04 > 0.5f) speed04 = speed04 * d.lockedDamping; else speed04 = 0.0f;
            if (local08 < -0.5f || local08 > 0.5f) local08 = local08 * d.lockedDamping; else local08 = 0.0f;
        }
        objMove({mx * dt, velocity.y * dt, mz * dt});
        const float inverse = 1.0f / dt;
        const float intendedX = (position.x - previous3C.x) * inverse, intendedZ = (position.z - previous3C.z) * inverse;
        const uint8_t walls = groundHits(frames);
        velocity = {(position.x - previous3C.x) * inverse, (position.y - previous3C.y) * inverse, (position.z - previous3C.z) * inverse};
        if (ceiling534 && 0.0f < velocity.y) velocity.y = 0.0f;
        const int16_t facing = halfTurn13E ? halfTurnHeading13C : heading11C;
        const auto local = math.rotateYPR({int16_t(-facing), int16_t(-orientation[1]), int16_t(-orientation[2])}, {intendedX, 0.0f, intendedZ});
        if (push149) {
            push149 = int8_t(push149 - frames);
            if (push149 < 0) push149 = 0;
        } else {
            float difference = speed04 - local.z;
            if (difference > 0.5f) speed04 = speed04 - (difference - 0.5f);
            if (difference < -0.5f) speed04 = speed04 - (difference + 0.5f);
            difference = local08 - local.x;
            if (difference > 0.5f) local08 = local08 - (difference - 0.5f);
            if (difference < -0.5f) local08 = local08 - (difference + 0.5f);
        }
        if (walls) {
            speed04 = std::clamp(speed04, -2.0f, 2.0f);
            local08 = std::clamp(local08, -2.0f, 2.0f);
            lateral10 = std::clamp(lateral10, -1.75f, 1.75f);
            blocked199 = 1;
        } else blocked199 = 0;
    }
    // objMoveXYZ: track bounds check (restart not ported), then move.
    void objMove(const Vec3f &delta) {
        const auto &e = query_.track().extents;  // minX, maxX, minY, maxY, minZ, maxZ
        const float nx = position.x + delta.x;
        bool outside = false;
        if ((float(e[1]) + 1000.0f) < nx) outside = true;
        if (position.x < (float(e[0]) - 1000.0f)) outside = true;
        if ((float(e[3]) + 3000.0f) < position.y) outside = true;
        if (position.y < (float(e[2]) - 1000.0f)) outside = true;
        if ((float(e[5]) + 1000.0f) < position.z) outside = true;
        if (position.z < (float(e[4]) - 1000.0f)) outside = true;
        if (outside) throw std::runtime_error("Juno left the track bounds; controlRestartPlayer is not ported");
        position = {nx, position.y + delta.y, position.z + delta.z};
    }
    // controlGroundHits for the five Juno spheres without object hit models.
    uint8_t groundHits(int32_t frames) {
        placeSpheres();
        std::array<Vec3f, 5> ends{}, offsets{};
        std::array<float, 5> radii{};
        std::array<uint16_t, 5> flags{};
        uint8_t skip = skip531;
        for (size_t i = 0; i < 5; ++i, skip >>= 1) {
            ends[i] = sphereCurrent3C4[i];
            radii[i] = data_->spheres[i].radius;
            flags[i] = data_->spheres[i].flags;
            if (skip & 1) flags[i] = uint16_t(flags[i] | 0x40);
            offsets[i] = {ends[i].x - position.x, ends[i].y - position.y, ends[i].z - position.z};
        }
        query_.makePolylist(5, spherePrevious484.data(), ends.data(), radii.data(), data_->exclude, data_->include);
        std::array<TrackHit, 5> results{};
        int32_t nearest = -1;
        Vec3f accumulated;
        const uint32_t mask = query_.playerIntersect(position, spherePrevious484.data(), ends.data(), radii.data(), offsets.data(),
                                                     flags.data(), results.data(), 5, nearest, accumulated);
        lastCollisionMask = mask;
        uint8_t result = 0;
        bool squeezed = false, stuck = false;
        if ((mask & 0x11110000u) == 0x11110000u) { position = safe524; result = 2; squeezed = true; }
        else if ((mask & 0xFFFF0000u) == 0xFFFF0000u) { position = safe524; result = 2; stuck = true; }
        int32_t ledge = -1;
        floor532 = wall533 = ceiling534 = 0;
        groundFlag184 = 1;
        surfaceFlags520 = 0;
        Vec3f floorSum;
        uint32_t bits = mask;
        uint8_t bit = 1;
        for (size_t i = 0; i < 5; ++i, bit = uint8_t(bit << 1), bits >>= 1) {
            const auto &r = results[i];
            const bool hit = bits & 1;
            if (hit && (r.type & 1)) {
                if (r.type & 0x12) {
                    if (r.edgeSpecial && (r.surfaceFlags & 0xCE000000u)) ledge = int32_t(i);
                    else {
                        floor532 |= bit;
                        const bool condition = ((r.type & 2) && (r.type & 0x10)) || !(r.objectKind < 2);
                        if (condition && (flags[i] & 0x22)) groundFlag184 = 0;
                    }
                }
                if (r.type & 0x48) ceiling534 |= bit;
                if (r.type & 0x24) {
                    wall533 |= bit;
                    result |= 1;
                    if (r.type & 0x20) groundFlag184 = 0;
                }
                if (r.type & 0x80) {
                    if (flags[i] & 0x22) floor532 |= bit;
                    stuck = true;
                }
            }
            if (hit && (r.type & 0x12)) floorSum = {floorSum.x + r.normal.x, floorSum.y + r.normal.y, floorSum.z + r.normal.z};
            surfaceFlags520 |= r.surfaceFlags;
        }
        Vec3f average;
        if (mask) {
            const float length = std::sqrt((floorSum.x * floorSum.x) + (floorSum.y * floorSum.y) + (floorSum.z * floorSum.z));
            if (length > 0.0f) average = {floorSum.x / length, floorSum.y / length, floorSum.z / length};
        }
        tilt(average, frames);
        auto pushAlong = [&](float x, float z, int8_t ticks, float strength) {
            const float h = (x * x) + (z * z);
            if (h > 0.0f) {
                const float l = std::sqrt(h);
                push149 = ticks;
                push6C = (x / l) * strength;
                push70 = (z / l) * strength;
            }
        };
        if (squeezed) {
            // Object platform reactions only; no objects are instantiated.
        } else if (stuck) pushAlong(accumulated.x, accumulated.z, 10, 5.0f);
        else if ((surfaceFlags520 & 0x8000) && state568 != 10) {
            const float h = (accumulated.x * accumulated.x) + (accumulated.z * accumulated.z);
            if (h > 0.0f && accumulated.y < std::sqrt(h)) pushAlong(accumulated.x, accumulated.z, 0x14, 5.0f);
        } else if (nearest >= 0) pushAlong(results[nearest].normal.x, results[nearest].normal.z, 10, 20.0f);
        else if (ledge != -1 && velocity.y < 0.0f) pushAlong(results[ledge].normal.x, results[ledge].normal.z, 5, 5.0f);
        spherePrevious484 = ends;
        return result;
    }
    // func_800344C8 for character type 0: slope alignment only in states 2/10.
    void tilt(const Vec3f &normal, int32_t frames) {
        const auto &math = data_->math;
        int16_t pitch = 0, roll = 0;
        if (state568 == 2 || state568 == 10) {
            const float s = math.sinf(int16_t(-orientation[0])), c = math.cosf(int16_t(-orientation[0]));
            const float across = (normal.z * c) - (normal.x * s);
            roll = int16_t(math.arctanf(-((normal.z * s) + (normal.x * c)), normal.y));
            pitch = int16_t(math.arctanf(across, normal.y));
        }
        pitch = int16_t(pitch + lean11E);
        roll = int16_t(roll + lean120);
        const float fraction = 1.0f - OriginalMath::powerf(0.96f, frames);
        orientation[2] = OriginalMath::dAngle(orientation[2], roll, fraction);
        orientation[1] = OriginalMath::dAngle(orientation[1], pitch, fraction);
    }
};
}
