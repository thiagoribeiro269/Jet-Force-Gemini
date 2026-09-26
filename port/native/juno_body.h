#pragma once
#include "track_collision.h"
#include "juno_selection.h"
#include "original_input.h"
#include <optional>

namespace jfg_native {

// Juno movement recovered from the original player code and ported to PC
// structures. Sources (static reading, bytes audited by movement_assets.py):
// objObjectsTick (previous position), boyControl (overlay 16), its walking
// state 0x2708, crouch states 0x2EB4/0x321C, air state 0x3464, strafe, rolls
// and lateral decay 0x4934 with controlMakeV and controlCeiling, move machine
// 0x5120, movement 0x5BB8, controlPlatform, controlMakeGravity,
// controlHalfTurn, controlWalkingBack, controlGroundHits, func_80035628,
// func_800344C8, objMoveXYZ, objAnimDframe/objAnimSetMove, mathRnd,
// controlReadJoypad, controlUpdatePlayerAim without targets, the fire
// decision of controlUpdateWeapon/boyCanFire, the aim states 0x4440/0x3F30
// and the joint turns 0x6290/0x3DB0/0x4840. One call is one original
// 1/60-second frame (frames = 1). Fields keep original offsets.
struct JunoSphere { Vec3f offset; float radius = 0; uint8_t flags = 0, rotate = 0; };
struct JunoPhysicsData {
    std::array<JunoSphere, 5> spheres{};
    std::array<uint8_t, 4> masks{};  // +0x531 skip, feet (+5), +6, +7 of the character table
    uint32_t exclude = 0, include = 0;
    std::array<float, 4> gravityCharacter{};
    std::array<float, 3> gravityState{};
    float turnAimRate = 0, turnAimTarget = 0, turnRate = 0, turnTarget = 0, turnFloor = 0, halfTurnFactor = 0, speedRate = 0,
          brakeTrigger = 0, leanFactor = 0, lateralDecay = 0, lateralZeroLow = 0, lateralZeroHigh = 0, slopeBias = 0,
          slopeLimit = 0, lockedDamping = 0, jumpAnimFloor = 0, airSpeedScale = 0, airSpeedBase = 0, airTurnScale = 0, airTurnBase = 0,
          strafeLeftRate = 0, strafeRightRate = 0, slideLateralDecay = 0, crouchSpeedDecay = 0, crouchStickScale = 0,
          crouchSpeedRate = 0, crouchTurnRate = 0, rollRate3 = 0, rollRate4 = 0, rollRate1 = 0, rollRate2 = 0,
          aimForwardRate = 0, aimBackRate = 0, aimSpeedDecay = 0, aimZeroLow = 0, aimZeroHigh = 0, aimSmoothing = 0,
          crouchAimSideDecay = 0, crouchAimSideLow = 0, crouchAimSideHigh = 0, crouchAimSpeedDecay = 0, crouchAimSpeedLow = 0,
          crouchAimSpeedHigh = 0, crouchAimSmoothing = 0;
    uint32_t rngSeed = 0;
    std::array<float, 52> rates{};
    std::array<float, 14> thresholds{};  // data +0xA44..+0xA78
    OriginalMath math;
    std::array<ControlModeKeys, 2> controlModes{};  // Normal, Expert
    uint32_t weaponCount = 0, currentWeapon = 0, weaponMask = 0;  // mainSetDefaultCharacter
    uint32_t gunWeight = 0;  // controlPlayerGunWeight for the pistol and Juno
    // Overlay 16 data +0x1F4: collision profile per move (controlSetTransition).
    struct CollisionProfile { uint8_t skip = 0, feet = 0, mask6 = 0, mask7 = 0; int32_t frames = 0; std::array<JunoSphere, 5> spheres{}; };
    std::array<CollisionProfile, 7> profiles{};
    std::array<std::array<float, 12>, 2> rollCurves{};  // data +0x78C (crouch-walk rolls), +0x7BC (crouch rolls)
    std::array<float, 20> aimTurn{};  // D_800A2E60: controlGetManualAim turn per stick step past 45
    std::array<uint8_t, 52> blendSteps{};  // clip header byte 1 low nibble, per move row
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
    if (bytes.size() < 16 || std::memcmp(take(8), "JFGPHY5\0", 8)) throw std::runtime_error("Wrong Juno physics format");
    if (u32() != 5 || u32() != 44) throw std::runtime_error("Unsupported Juno physics profile");
    auto data = std::make_shared<JunoPhysicsData>();
    auto sphereFrom = [&](JunoSphere &sphere) {
        sphere.offset = {f32(), f32(), f32()}; sphere.radius = f32();
        sphere.flags = *take(1); sphere.rotate = *take(1);
        if (take(2)[0] | bytes[at - 1]) throw std::runtime_error("Nonzero Juno sphere padding");
        if (!(sphere.radius > 0) || sphere.radius > 64) throw std::runtime_error("Invalid Juno sphere radius");
    };
    for (auto &sphere : data->spheres) sphereFrom(sphere);
    std::memcpy(data->masks.data(), take(4), 4);
    data->exclude = u32(); data->include = u32();
    for (auto &value : data->gravityCharacter) value = f32();
    for (auto &value : data->gravityState) value = f32();
    for (float *value : {&data->turnAimRate, &data->turnAimTarget, &data->turnRate, &data->turnTarget, &data->turnFloor,
                         &data->halfTurnFactor, &data->speedRate, &data->brakeTrigger, &data->leanFactor, &data->lateralDecay,
                         &data->lateralZeroLow, &data->lateralZeroHigh, &data->slopeBias, &data->slopeLimit, &data->lockedDamping,
                         &data->jumpAnimFloor, &data->airSpeedScale, &data->airSpeedBase, &data->airTurnScale, &data->airTurnBase,
                         &data->strafeLeftRate, &data->strafeRightRate, &data->slideLateralDecay, &data->crouchSpeedDecay,
                         &data->crouchStickScale, &data->crouchSpeedRate, &data->crouchTurnRate, &data->rollRate3, &data->rollRate4,
                         &data->rollRate1, &data->rollRate2, &data->aimForwardRate, &data->aimBackRate, &data->aimSpeedDecay,
                         &data->aimZeroLow, &data->aimZeroHigh, &data->aimSmoothing, &data->crouchAimSideDecay,
                         &data->crouchAimSideLow, &data->crouchAimSideHigh, &data->crouchAimSpeedDecay, &data->crouchAimSpeedLow,
                         &data->crouchAimSpeedHigh, &data->crouchAimSmoothing})
        *value = f32();
    data->rngSeed = u32();
    for (auto &value : data->rates) value = f32();
    for (auto &value : data->thresholds) value = f32();
    std::array<float, 1025> sine{};
    std::array<int16_t, 1025> arctan{};
    for (auto &value : sine) value = f32();
    for (auto &value : arctan) { const auto *p = take(2); value = int16_t(p[0] | p[1] << 8); }
    while (at % 4) if (*take(1)) throw std::runtime_error("Nonzero Juno physics padding");
    for (auto &mode : data->controlModes) for (auto &word : mode.word) word = u32();
    data->weaponCount = u32(); data->currentWeapon = u32(); data->weaponMask = u32();
    data->gunWeight = u32();
    for (auto &profile : data->profiles) {
        const auto *m = take(4);
        profile.skip = m[0]; profile.feet = m[1]; profile.mask6 = m[2]; profile.mask7 = m[3];
        profile.frames = int32_t(u32());
        if (profile.frames < 1 || profile.frames > 127) throw std::runtime_error("Invalid Juno collision profile");
        for (auto &sphere : profile.spheres) sphereFrom(sphere);
    }
    for (auto &curve : data->rollCurves) for (auto &value : curve) value = f32();
    for (auto &value : data->aimTurn) value = f32();
    std::memcpy(data->blendSteps.data(), take(52), 52);
    while (at % 4) if (*take(1)) throw std::runtime_error("Nonzero Juno physics padding");
    if (at != bytes.size()) throw std::runtime_error("Trailing Juno physics payload");
    data->math = OriginalMath(sine, arctan);
    const auto &normal = data->controlModes[0], &expert = data->controlModes[1];
    if (normal.fire() != Pad::Z || normal.jump() != Pad::A || normal.crouch() != Pad::B || normal.strafeLeft() != Pad::CLeft ||
        normal.strafeRight() != Pad::CRight || expert.jump() != Pad::CUp || expert.crouch() != Pad::CDown ||
        data->weaponCount != 1 || data->currentWeapon != 0 || data->weaponMask != 1 || data->strafeLeftRate != 0.2f ||
        data->gunWeight != 1 || data->profiles[0].spheres[0].radius != 15.0f || data->profiles[1].feet != 7 ||
        data->rollCurves[0][11] != 32.0f || data->rollCurves[1][11] != 44.0f || data->aimTurn[19] != 380.0f || data->aimSpeedDecay != 0.95f ||
        data->blendSteps[16] != 10 || data->blendSteps[33] != 2 || data->crouchAimSideDecay != 0.85f)
        throw std::runtime_error("Juno control data differ from the inspected profile");
    if (data->exclude != 0xCE002000u || data->include != 0x02000000u || data->masks != std::array<uint8_t, 4>{0x18, 0x01, 0x08, 0x16} ||
        data->gravityCharacter[0] != 0.45f || data->gravityState[0] != 1.0f || data->speedRate != 0.95f || data->rates[0] != 0.015f)
        throw std::runtime_error("Juno physics values differ from the inspected profile");
    return data;
}

// One original frame of controller input for Juno: the joyRead result for
// the player's controller, the active camera yaw (*controlcam) and the menu
// control mode (frontGetTargetControl: 0 Normal, 1 Expert).
struct JunoControl {
    JoypadFrame joypad;
    int16_t cameraYaw = 0;
    uint8_t controlMode = 0;
    int16_t cameraOffset10A = 0;  // the free camera's C-button offset (a player field in the original)
};

// One entry of the player's joint-turn list (controlPlayerTiltList, player
// +0x240), in the command format of gen_anim_data (func_80074D60). Command 0,
// the only one the ported states write, adds `value` to the s16 channel at
// byte offset `offset` of the decoded animation channels.
struct JointTurn { uint16_t offset = 0; int16_t value = 0; };
// MIPS sra on a 32-bit value (floor division by 2^n), without relying on
// implementation-defined signed shifts.
inline int32_t shiftRight(int32_t value, unsigned n) { return value >= 0 ? value >> n : ~(~value >> n); }

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
    uint8_t jumpCharge57B = 0, jumpReleased58E = 0, groundFlag184 = 1, blocked199 = 0, strafing56C = 0, roll584 = 0;
    int8_t firing1F4 = 0;                   // set while the fire key is held on the ground
    // Values left by controlReadJoypad this frame (disablejoy zeroes them);
    // the free camera reads controlKeys after the character routine.
    uint16_t controlKeys = 0, controlDkeys = 0;
    int32_t controlXjoy = 0, controlYjoy = 0, controlAbsX = 0, controlAbsY = 0;
    uint8_t controlMode = 0;
    // Aim fields (+0x1C6..+0x1E8): aim direction, joint-turn targets and the
    // manual-aim turn speeds.
    int16_t aimYaw1C6 = 0, aimPitch1C8 = 0, aimYaw1CA = 0, aimPitch1CC = 0, joint1CE = 0, joint1D0 = 0, joint1DC = 0, joint1DE = 0,
            joint1E0 = 0, aimPitch1E2 = 0;
    float turn1E4 = 0, turn1E8 = 0;
    // Joint turns: boyControl rebuilds the list each frame and
    // modGenAnimMatrices applies it to the pose of that frame. +0x580 is the
    // torso twist of 0x4840 (an s32 that only this routine writes).
    int32_t twist580 = 0;
    std::array<JointTurn, 8> jointTurns{};
    uint8_t jointTurnCount = 0;
    // +0x19C: controlFadePlayer's fade counter; the model draw turns it into
    // the object's opacity (+0x39) through func_80015CB8.
    int8_t fade19C = 0;
    // Camera fields the character routine writes (+0x10A offset, +0x104
    // orbit): applied by the caller to the camera before it runs.
    bool clearCameraOffset = false, setCameraOrbit = false;
    int16_t cameraOrbit104 = 0;
    // Model instance +0x5E/+0x5C: clip blend counter, set by objAnimSetMove
    // and run down once per frame by modGenAnimMatrices.
    int16_t blend5E = 0, blendStep5C = 0;
    float push6C = 0, push70 = 0, fallStart57C = 0;
    uint8_t floor532 = 0, wall533 = 0, ceiling534 = 0, skip531 = 0;
    uint32_t surfaceFlags520 = 0;
    std::array<Vec3f, 5> sphereBase364, sphereCurrent3C4, spherePrevious484, delta424;
    // +0x360: -1 is the controlPlayerInit profile (D_800A1BF8[0]); from the first
    // func_overlay_16_01004F78 on, an overlay 16 profile. +0x535 counts the
    // frames left to move the sphere offsets toward it.
    int8_t profile360 = -1, transition535 = 0;
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
        placeSpheres(0);
        spherePrevious484 = sphereCurrent3C4;
        previous3C = safe524 = position;
        move3B = 16;  // objAnimSetMove(arg0, 0x10, 0) for types 0/1: no remap, no profile change
        startBlend(16);
    }
    const TrackQuery &query() const { return query_; }
    // objMoveXYZ(player, dx, 0, dz) issued by the free camera (func_8002CF78).
    void pushByCamera(float dx, float dz) { objMove({dx, 0.0f, dz}); }
    const JunoPhysicsData &data() const { return *data_; }
    uint32_t clipId() const { return selector_.local(move3B).clip; }
    // func_80015CB8 in single player, with the camera on this player: the
    // opacity the draw list writes to the object (+0x39) before the model is
    // drawn. (+0x5C0 would force 255; the ported subset never sets it.)
    uint8_t opacity() const {
        if (state568 == 0xA) return uint8_t(0xFF - (fade19C * 2));
        const int32_t value = 0xFF - (fade19C * 4);
        return uint8_t(value < 0 ? 0 : value);
    }
    // Sphere definitions and floor masks of the current collision profile (+0x360).
    const JunoSphere &sphereDef(size_t i) const { return profile360 < 0 ? data_->spheres[i] : data_->profiles[size_t(profile360)].spheres[i]; }
    uint8_t profileMask(unsigned k) const {  // 1: +5 feet, 2: +6, 3: +7
        if (profile360 < 0) return data_->masks[k];
        const auto &p = data_->profiles[size_t(profile360)];
        return k == 1 ? p.feet : k == 2 ? p.mask6 : p.mask7;
    }
    bool grounded() const { return profileMask(1) & floor532; }

    // One original frame for player type 0.
    void tick(const JunoControl &control) {
        validatePad(control.joypad.pad);
        if (control.controlMode > 1) throw std::runtime_error("Unknown original control mode");
        const auto &math = data_->math;
        constexpr int32_t frames = 1;
        constexpr float dt = 1.0f;
        ++this->frames;
        previous3C = position;                  // objObjectsTick
        bool disabled = false;                  // disablejoy, cleared by controlPlayer each frame
        safe524 = previous3C;                   // controlPlatform without a platform object
        lean11E = 0; lean120 = 0;
        // controlPlayer: controlModeKeys from frontGetTargetControl.
        controlMode = control.controlMode;
        cameraOffsetIn_ = control.cameraOffset10A;
        clearCameraOffset = setCameraOrbit = false;
        jointTurnCount = 0;                     // boyControl: tilt list pointer = player + 0x240
        const auto &mode = data_->controlModes[controlMode];
        // boyControl: legitimate-ROM joystick scale; unk189 variants inactive.
        const float scale = 0.0625f;
        if (landingLock56B) {
            disabled = true;
            landingLock56B = int8_t(landingLock56B - frames);
            if (landingLock56B < 0) landingLock56B = 0;
        }
        // controlReadJoypad, after the locks above set disablejoy.
        const auto input = controlReadJoypad(control.joypad, disabled);
        controlKeys = input.keys; controlDkeys = input.dkeys; controlXjoy = input.xjoy; controlYjoy = input.yjoy;
        controlAbsX = input.absX; controlAbsY = input.absY;
        const int32_t stickX = input.xjoy, stickY = input.yjoy;
        const bool jumpHeld = input.keys & mode.jump(), jumpPressed = input.dkeys & mode.jump();
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
        const uint8_t feet = profileMask(1);
        if ((state568 == 0 || state568 == 5 || state568 == 11) && !(feet & floor532)) becomeAirborne(frames);
        else if ((state568 == 2 || state568 == 1) && !(feet & floor532) && !(profileMask(2) & floor532) && !(profileMask(3) & floor532))
            becomeAirborne(frames);
        else airborne185 = 0;
        if (((feet & floor532) && state568 != 3) || state568 == 4) fallStart57C = position.y;
        aimWithoutTargets();
        if (state568 != 0xA) fadePlayer(frames);
        // Water and lava from trackPolyHeight(x, z, +0x5C, 0x8000A000): not ported.
        float surfaceHeight = 0;
        if (query_.polyHeight(position.x, position.z, surfaceHeight, 0x8000A000u))
            throw NotPortedError("Water or lava surfaces are not ported", "Água e lava ainda não foram portadas.");
        if (state568 != 0 && state568 != 2) {
            halfTurn13E = 0;
            if (state568 != 1) skid576 = 0;
        }
        if (state568 == 0) walk(speed, direction, frames, jumpPressed, stickX, stickY, scale, mode);
        else if (state568 == 1) crouch(speed, frames, mode);
        else if (state568 == 2) crouchWalk(speed, direction, frames, jumpPressed, stickX, stickY, mode);
        else if (state568 == 3) air(speed, direction, frames, jumpHeld);
        else if (state568 == 0xB) aimStand(frames, mode);
        else if (state568 == 5) aimCrouch(frames, mode);
        else throw NotPortedError("Juno state outside the ported walking/air subset", "Este estado do Juno ainda não foi portado.");
        animate(dt);
        move(gravity, frames, dt, disabled);
        // After the hang checks (no ledges in Forest First) and 0x2220 (hits
        // on Juno, none without enemies): 0x6290 outside the aim, air and
        // state 8 routines, then the torso twist 0x4840. The list ends there
        // (weapon bits 0x40 of +0x540 need a weapon other than the pistol).
        if (state568 != 5 && state568 != 0xB && state568 != 3 && state568 != 8) followAim(frames);
        strafeTwist(frames);
        // modGenAnimMatrices at the end of boyControl (objResetAnimModels marks
        // the model every frame): the clip blend runs down one step.
        if (blend5E != 0) {
            blend5E = int16_t(blend5E - blendStep5C);
            if (blend5E < 0) blend5E = 0;
        }
        // controlUpdateWeapon, end of boyControl: with the pistol, a held fire
        // key starts a shot whenever boyCanFire allows it. Shots are not ported.
        if ((controlKeys & mode.fire()) && canFire())
            throw NotPortedError("Pistol shot (controlUpdateWeapon) is not ported", "Tiro (Z) ainda não foi portado.");
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
    int16_t cameraOffsetIn_ = 0;

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
        state.flag1F4 = firing1F4 != 0;
        const float a = magnitude(speed04), b = magnitude(lateral10);
        if (std::max(a, b) < 0.1f && !state.flag1FA && !state.flag1F9 && !state.flag1F4 && !state.flag198)
            state.idleDraw = uint32_t(random_.next(16, 19));
        return selector_.choose(state);
    }
    // func_overlay_16_01004F78: remap by the pistol's gun weight (firing
    // column while +0x1F4 runs), objAnimSetMove only on change, then
    // controlSetTransition to the destination row's collision profile.
    void requestMove(uint32_t requested, float fraction) {
        JunoSelectionState remap;
        remap.gunWeight = data_->gunWeight != 0;
        remap.flag1F4 = firing1F4 != 0;
        const auto selected = selector_.resolve(requested, remap);
        if (forcedMove_ != -1 || selected.local != move3B) {
            if (fraction > 1.0f) fraction = 1.0f;
            else if (fraction < 0.0f) fraction = 0.0f;
            move3B = selected.local;
            progress28 = fraction;
            ++moveChanges;
            startBlend(selected.local);
        }
        forcedMove_ = -1;
        transitionProfile = selected.transitionProfile;
        setTransition(selected.transitionProfile);
    }
    // objAnimSetMove: a clip with blend steps restarts the model's blend.
    void startBlend(uint32_t move) {
        const uint8_t steps = data_->blendSteps.at(move);
        if (steps) { blend5E = 0x3FF; blendStep5C = int16_t(0x3FF / steps); }
    }
    // controlSetTransition: a new profile sets the skip mask and moves the
    // sphere offsets toward its spheres over its frame count.
    void setTransition(uint32_t index) {
        if (index >= data_->profiles.size()) throw std::runtime_error("Juno collision profile outside the table");
        if (profile360 == int8_t(index)) return;
        const auto &p = data_->profiles[index];
        skip531 = p.skip;
        profile360 = int8_t(index);
        transition535 = int8_t(p.frames);
        const float frames = float(transition535);
        for (size_t i = 0; i < 5; ++i) {
            const auto &target = p.spheres[i].offset;
            delta424[i] = {(target.x - sphereBase364[i].x) / frames, (target.y - sphereBase364[i].y) / frames,
                           (target.z - sphereBase364[i].z) / frames};
        }
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
    // func_80035628: advance the profile transition, then sphere centres from
    // the base offsets, orientation and position.
    void placeSpheres(int32_t frames) {
        float step = 0.0f;
        if (transition535 > 0) {
            const int32_t left = transition535 - frames;
            if (left >= 0) { transition535 = int8_t(left); step = float(frames); }
            else { step = float(transition535); transition535 = 0; }
        }
        for (size_t i = 0; i < 5; ++i) {
            auto &base = sphereBase364[i];
            if (step > 0.0f) base = {base.x + (delta424[i].x * step), base.y + (delta424[i].y * step), base.z + (delta424[i].z * step)};
            Vec3f p = base;
            if (sphereDef(i).rotate) p = data_->math.rotateRPY(orientation, p);
            sphereCurrent3C4[i] = {p.x + position.x, p.y + position.y, p.z + position.z};
        }
    }
    // controlHalfTurn(obj, player, &speed, &direction, 0x6AAA, 0x71C, &skid, scale).
    void halfTurn(float &speed, int16_t &direction, bool jumpPressed, int32_t stickX, int32_t stickY, float scale, bool &skid) {
        const auto &math = data_->math;
        constexpr int16_t wide = 0x6AAA, narrow = 0x71C;
        skid = false;
        if (jumpPressed || walkingBack569 || (firing1F4 != 0 && controlMode == 0) || (state568 != 0 && state568 != 2)) {
            halfTurn13E = 0;
            return;
        }
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
    // func_overlay_16_01004934. Walking (arg2 0): the strafe keys
    // (controlModeKeys +0x14/+0x18) push +0x10 sideways up to 2.5. Crouched
    // (arg2 1) or crouch-walking (arg2 2): they start a roll (+0x584 1..4)
    // whose sideways speed follows a distance curve (controlMakeV) along the
    // roll clip. Otherwise +0x10 decays.
    void strafe(int32_t arg2, int32_t frames, const ControlModeKeys &mode) {
        const auto &d = *data_;
        strafing56C = 0;
        if (roll584 >= 1 && roll584 <= 4 && progress28 == 1.0f) { roll584 = 0; lateral10 = 0.0f; }
        if (roll584 == 0) {
            const bool left = controlKeys & mode.strafeLeft(), right = !left && (controlKeys & mode.strafeRight());
            if (arg2 != 0) {
                if (left || right) {
                    roll584 = uint8_t(arg2 == 2 ? (left ? 3 : 4) : (left ? 1 : 2));
                    requestMove(arg2 == 2 ? (left ? 0x32 : 0xC) : (left ? 0x31 : 0xB), 0.0f);
                    progress28 = 0.0f;
                    strafing56C = 1;
                }
            } else {
                uint32_t next = 0;
                if (left) {
                    lateral10 = lateral10 - (d.strafeLeftRate * float(frames));
                    if (lateral10 < -2.5f) lateral10 = -2.5f;
                    next = 9; strafing56C = 1;
                } else if (right) {
                    lateral10 = lateral10 + (d.strafeRightRate * float(frames));
                    if (lateral10 > 2.5f) lateral10 = 2.5f;
                    next = 0xA; strafing56C = 1;
                }
                if (strafing56C && magnitude(speed04) < magnitude(lateral10)) requestMove(next, progress28);
            }
        }
        const std::array<float, 12> *curve = nullptr;
        float rate = 0.0f;
        switch (roll584) {
            case 3: if (move3B == 0x32) { curve = &d.rollCurves[0]; rate = d.rollRate3; } break;
            case 4: if (move3B == 0xC) { curve = &d.rollCurves[0]; rate = d.rollRate4; } break;
            case 1: if (move3B == 0x31) { curve = &d.rollCurves[1]; rate = d.rollRate1; } break;
            case 2: if (move3B == 0xB) { curve = &d.rollCurves[1]; rate = d.rollRate2; } break;
            default: break;
        }
        if (curve) {
            const float from = progress28;
            float to = from + (rate * float(frames));
            if (to < 0.0f) to = 0.0f;
            if (to > 1.0f) to = 1.0f;
            lateral10 = controlMakeV(from, to, *curve, float(frames));
            if (roll584 == 3 || roll584 == 1) lateral10 = -lateral10;
            strafing56C = 1;
        }
        if (strafing56C) return;
        roll584 = 0;
        lateral10 = lateral10 * OriginalMath::powerf(d.lateralDecay, frames);
        if (d.lateralZeroLow < lateral10 && lateral10 < d.lateralZeroHigh) lateral10 = 0.0f;
    }
    // controlMakeV: distance travelled along a 0.1-step curve between two clip
    // positions, per frame. The indices truncate (FCSR round-toward-zero).
    static float controlMakeV(float from, float to, const std::array<float, 12> &curve, float frames) {
        const float a = to * 10.0f, b = from * 10.0f;
        const auto ia = size_t(int32_t(a)), ib = size_t(int32_t(b));
        if (ia > 10 || ib > 10) throw std::runtime_error("Roll curve index outside the table");
        const float at = ((curve[ia + 1] - curve[ia]) * (a - float(int32_t(a)))) + curve[ia];
        const float bt = curve[ib] + ((curve[ib + 1] - curve[ib]) * (b - float(int32_t(b))));
        return (at - bt) / frames;
    }
    // controlCeiling(x, y, z, 60, 15): a 15-unit sphere can rise 60 units
    // without a ceiling response (type 0x48), so Juno has room to stand.
    bool roomToStand() {
        const Vec3f start = position;
        Vec3f end{position.x, position.y + 60.0f, position.z};
        const float radius = 15.0f;
        query_.makePolylist(1, &start, &end, &radius, 0, 0);
        TrackHit hit;
        return !(query_.getIntersect(start, end, radius, 1, hit) && (hit.type & 0x48));
    }
    // Crouch state 1: func_overlay_16_01002EB4 (crouched, sliding, rolling).
    // Deep water (+0x60) would force standing; Forest First has none.
    void crouch(float speed, int32_t frames, const ControlModeKeys &mode) {
        const auto &d = *data_;
        if ((controlKeys & mode.jump()) && strafing56C == 0 && roomToStand()) {
            requestMove(chooseMove(), 0.0f);
            state568 = 0;
            skid576 = 0;
        } else if (skid576 != 0) {  // slide
            skid576 = int8_t(skid576 - frames);
            lateral10 = lateral10 * OriginalMath::powerf(d.slideLateralDecay, frames);
            if (skid576 <= 0) {
                skid576 = 0;
                speed04 = 0.0f;
                lateral10 = 0.0f;
                if (speed > 0.5f && !(controlKeys & Pad::R)) { state568 = 2; requestMove(0x16, 0.0f); }
                else requestMove(0xE, 0.0f);
            }
        }
        if (state568 == 1 && skid576 == 0) {
            if ((controlKeys & Pad::R) && canFire()) {  // crouched aim
                beginAim(5);
                return;
            }
            strafe(1, frames, mode);
            if (move3B != 0xB && move3B != 0x31) {
                if (controlKeys & mode.fire()) firing1F4 = 0xF;
                if (!grounded()) firing1F4 = 0;
                if (speed > 0.5f) { requestMove(0x16, 0.0f); state568 = 2; }
            }
        }
        speed04 = speed04 * OriginalMath::powerf(d.crouchSpeedDecay, frames);
    }
    // Crouch-walk state 2: func_overlay_16_0100321C.
    void crouchWalk(float speed, int16_t direction, int32_t frames, bool jumpPressed, int32_t stickX, int32_t stickY,
                    const ControlModeKeys &mode) {
        const auto &d = *data_;
        if ((controlKeys & mode.jump()) && strafing56C == 0 && roomToStand()) {
            requestMove(8, 0.0f);
            state568 = 0;
            return;
        }
        strafe(2, frames, mode);
        if (1.25f < speed) speed = 1.25f;  // controlWalkingBack(1.25, 1.25) without aim locks
        if (!walkingBack569) speed = -speed; else direction = int16_t(direction + 0x8000);
        bool skid = false;  // its skid result is not used here
        halfTurn(speed, direction, jumpPressed, stickX, stickY, d.crouchStickScale, skid);
        speed04 = ((1.0f - OriginalMath::powerf(d.crouchSpeedRate, frames)) * (speed - speed04)) + speed04;
        heading11C = OriginalMath::dAngle(heading11C, direction, 1.0f - OriginalMath::powerf(d.crouchTurnRate, frames));
    }
    // controlUpdatePlayerAim for states other than 5/0xB/0xA. No objects are
    // instantiated, so there is no target, aim lock or mathRnd call: the
    // firing counter +0x1F4 runs down and the aim follows the body. In the aim
    // states boyControl holds +0x1F4 at 1 instead.
    void aimWithoutTargets() {
        if (state568 == 5 || state568 == 0xB || state568 == 0xA) { firing1F4 = 1; return; }
        if (firing1F4 != 0) firing1F4 = int8_t(firing1F4 - 1);
        aimPitch1C8 = aimPitch1CC = orientation[1];
        aimYaw1C6 = aimYaw1CA = orientation[0];
    }
    // controlFadePlayer(player, NULL, frames) for the player's own camera.
    // The aim states fade Juno in, one step per frame up to 0x28 (arg1 is
    // NULL, so the 0x40 limit of weapon 7 never applies); otherwise the fade
    // runs out 16 per frame. Elsewhere the target-lock timer (+0x541 low
    // nibble) would also fade him, but only controlUpdatePlayerAim sets it,
    // with a target. States 4/6/7/8 (no fade), the lobby and static cameras
    // and the +0x19D/+0x1F9/+0x1FA/+0x198 flags are outside the ported subset.
    void fadePlayer(int32_t frames) {
        if (state568 == 0xB || state568 == 5) {
            fade19C = int8_t(fade19C + frames);
            if (0x28 < fade19C) fade19C = 0x28;
        } else if (fade19C != 0) {
            fade19C = int8_t(fade19C - (frames * 0x10));
            if (fade19C < 0) fade19C = 0;
        }
    }
    // Entering an aim state from walking (0x2708): the heading absorbs the
    // camera's C offset, which is cleared, and the aim fields restart.
    void beginAim(uint8_t state) {
        heading11C = int16_t(heading11C - cameraOffsetIn_);
        clearCameraOffset = true;
        aimPitch1E2 = 0; joint1E0 = 0; state568 = state; joint1CE = 0; joint1D0 = 0; turn1E4 = 0.0f; turn1E8 = 0.0f;
        aimYaw1C6 = orientation[0]; aimPitch1C8 = orientation[1];
        joint1DC = joint1CE; aimYaw1CA = aimYaw1C6; aimPitch1CC = aimPitch1C8; joint1DE = joint1D0;
    }
    // controlGetManualAim for a weapon without zoom (the pistol): the raw
    // stick moves the aim within +-40; past 45 the table turns the body
    // (+0x11C) or the aim pitch (+0x1E2). Returns the zoom flag (false).
    bool manualAim(int16_t yawRange, int16_t pitchRange, int16_t pitchLow, int16_t pitchHigh, int16_t &yawOut, int16_t &pitchOut,
                   int32_t frames) {
        const auto &table = data_->aimTurn;
        const float zoom = 1.0f;  // +0x1EC stays 1 without the zoom weapon; +0x1F0 is 0
        int32_t x = -controlAbsX, y = -controlAbsY;
        float turnYaw = 0.0f, turnPitch = 0.0f;
        const float inverse = 1.0f / zoom;
        const float scale = 1.0f / (((zoom - 1.0f) * (zoom * 0.5f)) + 1.0f);
        const auto yawLimit = int16_t(int32_t(float(yawRange) * scale)), pitchLimit = int16_t(int32_t(float(pitchRange) * scale));
        auto step = [&](int32_t index) { return table[size_t(index >= 0x14 ? 0x13 : index)]; };
        if (x < -0x28) {
            if (x < -0x2D && joint1CE < (0x2D8 - yawLimit)) turnYaw = -step(-0x2D - x) * inverse;
            x = -0x28;
        }
        if (x >= 0x29) {
            if (x >= 0x2E && (yawLimit - 0x2D8) < joint1CE) turnYaw = step(x - 0x2D) * inverse;
            x = 0x28;
        }
        if (y < -0x28) {
            if (y < -0x2D && joint1E0 < (0x2D8 - pitchLimit)) turnPitch = -step(-0x2D - y) * inverse;
            y = -0x28;
        }
        if (y >= 0x29) {
            if (y >= 0x2E && (pitchLimit - 0x2D8) < joint1E0) turnPitch = step(y - 0x2D) * inverse;
            y = 0x28;
        }
        const float rate = 1.0f - OriginalMath::powerf(0.8334f, frames);
        turn1E4 = turn1E4 + ((turnYaw - turn1E4) * rate);
        turn1E8 = turn1E8 + ((turnPitch - turn1E8) * rate);
        if (turnYaw == 0.0f && turn1E4 > -1.0f && turn1E4 < 1.0f) turn1E4 = 0.0f;
        if (turnPitch == 0.0f && turn1E8 > -1.0f && turn1E8 < 1.0f) turn1E8 = 0.0f;
        yawOut = int16_t((yawLimit * x) / 40);
        pitchOut = int16_t((pitchLimit * y) / 40);
        heading11C = int16_t(heading11C + int32_t(turn1E4 * float(frames)));
        aimPitch1E2 = int16_t(aimPitch1E2 + int32_t(turn1E8 * float(frames)));
        if (aimPitch1E2 < pitchLow) aimPitch1E2 = pitchLow;
        if (pitchHigh < aimPitch1E2) aimPitch1E2 = pitchHigh;
        return false;
    }
    // Aim smoothing shared by both aim states: joint-turn targets and the aim
    // direction follow the manual aim.
    void smoothAim(int16_t yaw, int16_t pitch, float rate) {
        joint1E0 = OriginalMath::dAngle(joint1E0, pitch, rate);
        pitch = int16_t(pitch + aimPitch1E2);
        joint1DC = OriginalMath::dAngle(joint1DC, yaw, rate);
        joint1DE = OriginalMath::dAngle(joint1DE, pitch, rate);
        joint1CE = OriginalMath::dAngle(joint1CE, yaw, rate);
        joint1D0 = OriginalMath::dAngle(joint1D0, pitch, rate);
        aimYaw1C6 = int16_t(joint1CE + orientation[0]);
        aimYaw1CA = aimYaw1C6;
        aimPitch1C8 = int16_t(joint1D0 + orientation[1]);
        aimPitch1CC = aimPitch1C8;
    }
    // Crouched aim state 5: func_overlay_16_01003F30. R released returns to
    // crouching; A (or Expert's walk keys) stands into the standing aim. Deep
    // water would also stand; Forest First has none.
    void aimCrouch(int32_t frames, const ControlModeKeys &mode) {
        const auto &d = *data_;
        skid576 = 0;
        halfTurn13E = 0;
        if (!(controlKeys & Pad::R)) {  // and no throw lock (+0x577)
            state568 = 1;
            setCameraOrbit = true;
            cameraOrbit104 = int16_t(0x8000 - orientation[0]);
            return;
        }
        lateral10 = lateral10 * OriginalMath::powerf(d.crouchAimSideDecay, frames);
        if (d.crouchAimSideLow < lateral10 && lateral10 < d.crouchAimSideHigh) lateral10 = 0.0f;
        speed04 = speed04 * OriginalMath::powerf(d.crouchAimSpeedDecay, frames);
        if (d.crouchAimSpeedLow < speed04 && speed04 < d.crouchAimSpeedHigh) speed04 = 0.0f;
        int16_t yaw = 0, pitch = 0;
        manualAim(0xE38, 0xE38, -0x2AAA, 0x2AAA, yaw, pitch, frames);
        const float rate = 1.0f - OriginalMath::powerf(d.crouchAimSmoothing, frames);
        if (aimPitch1E2 < -0xA80) aimPitch1E2 = -0xA80;
        if (aimPitch1E2 >= 0x1556) aimPitch1E2 = 0x1555;
        smoothAim(yaw, pitch, rate);
        // 0x3F30 writes its own list before the stand checks; the recoil term
        // (+0x1F6/+0x1F7) is 0 without shots.
        const int16_t recoil = 0;
        addTurn(6, int16_t(joint1D0 + recoil));
        addTurn(8, joint1CE);
        addTurn(0x12, recoil);
        addTurn(0x3C, int16_t(joint1DE - joint1D0));
        addTurn(0x3E, int16_t(joint1DC - joint1CE));
        if ((controlMode != 0 && ((mode.word[7] | mode.word[8]) & controlKeys)) || (controlDkeys & mode.jump())) {
            state568 = 0xB;
            requestMove(chooseMove(), 0.0f);
        }
    }
    // Standing aim state 0xB: func_overlay_16_01004440. R released returns to
    // walking and points the free camera's orbit behind Juno; otherwise the
    // torso, head and arm turn toward the aim (0x3DB0).
    void aimStand(int32_t frames, const ControlModeKeys &mode) {
        const auto &d = *data_;
        skid576 = 0;
        halfTurn13E = 0;
        if (!(controlKeys & Pad::R)) {  // and no throw lock (+0x577), which needs weapons
            state568 = 0;
            setCameraOrbit = true;
            cameraOrbit104 = int16_t(0x8000 - orientation[0]);
            return;
        }
        if (controlMode != 0 && ((mode.word[7] | mode.word[8]) & controlKeys)) {  // Expert: walk while aiming
            if (mode.word[7] & controlKeys) {
                speed04 = speed04 - (d.aimForwardRate * float(frames));
                const float limit = -(65.0f * 0.0625f);
                if (speed04 < limit) speed04 = limit;
            } else {
                speed04 = speed04 + (d.aimBackRate * float(frames));
                if (speed04 > 3.0f) speed04 = 3.0f;
            }
        } else {
            speed04 = speed04 * OriginalMath::powerf(d.aimSpeedDecay, frames);
            if (d.aimZeroLow < speed04 && speed04 < d.aimZeroHigh) speed04 = 0.0f;
        }
        strafe(0, frames, mode);
        int16_t yaw = 0, pitch = 0;
        manualAim(0xE38, 0xE38, -0x2AAA, 0x2AAA, yaw, pitch, frames);  // no zoom: camSetZoom is not called
        smoothAim(yaw, pitch, 1.0f - OriginalMath::powerf(d.aimSmoothing, frames));
        torsoTurns(joint1D0, joint1CE, joint1DE, joint1DC);  // +0x1F6 recoil wobble needs shots
    }
    void addTurn(uint16_t offset, int16_t value) {
        if (jointTurnCount >= jointTurns.size()) throw std::runtime_error("Juno joint-turn list overflow");
        jointTurns[jointTurnCount++] = {offset, value};
    }
    // func_overlay_16_01003DB0: torso (channels 3/4 and 9), head and arm
    // (30/31) turns from the aim pitch and yaw and the recoil turns. The half
    // pitch is cvt.w.s under round-toward-zero.
    void torsoTurns(int16_t pitch, int16_t yaw, int16_t recoilPitch, int16_t recoilYaw) {
        int32_t a0 = pitch, a2 = recoilPitch;
        if (a0 < -0x2AAA) a0 = -0x2AAA;
        if (a2 >= 0x2AAB) a2 = 0x2AAA;
        if (a2 < -0x2AAA) a2 = -0x2AAA;
        const int32_t half = OriginalMath::truncate(float(a0) * 0.5f);
        addTurn(6, int16_t(half));
        addTurn(8, yaw);
        addTurn(0x12, int16_t(a0 - half));
        addTurn(0x3C, int16_t(a2 - half));
        addTurn(0x3E, int16_t(int32_t(recoilYaw) - int32_t(yaw)));
    }
    // func_overlay_16_01006290: outside the aim states the joint turns
    // (+0x1D0 pitch, +0x1CE yaw) follow the aim relative to the body, zero
    // while crouch-walking; the recoil turns (+0x1DE/+0x1DC) settle to zero
    // (+0x1F5 recoil frames are set only by shots). While +0x1F4 is set the
    // torso and head keep turning (0x3DB0).
    void followAim(int32_t frames) {
        int32_t pitch = 0, yaw = 0;
        if (state568 != 0xA && state568 != 2) {
            pitch = int16_t(aimPitch1C8 - orientation[1]);
            if (pitch < -0x2AAA) pitch = -0x2AAA;
            if (pitch >= 0x2AAB) pitch = 0x2AAA;
            yaw = int16_t(aimYaw1C6 - orientation[0]);
            if (yaw < -0x1000) yaw = -0x1000;
            if (yaw >= 0x1001) yaw = 0x1000;
        }
        for (int32_t i = 0; i < frames; ++i) {
            joint1D0 = int16_t(joint1D0 + shiftRight(pitch - joint1D0, 2));
            joint1CE = int16_t(joint1CE + shiftRight(yaw - joint1CE, 2));
        }
        for (int32_t i = 0; i < frames; ++i) {
            joint1DE = int16_t(joint1DE + shiftRight(0 - joint1DE, 4));
            joint1DC = int16_t(joint1DC + shiftRight(0 - joint1DC, 4));
        }
        if (firing1F4 != 0) torsoTurns(joint1D0, joint1CE, joint1DE, joint1DC);
    }
    // mathDiffAngle on full registers: one 16-bit wrap of to - from.
    static int32_t diffAngle(int32_t from, int32_t to) {
        int32_t difference = to - from;
        if (difference >= 0x8000) difference -= 0x10000;
        else if (difference < -0x7FFF) difference += 0x10000;
        return difference;
    }
    // func_overlay_16_01004840, every frame: moving more forward than
    // sideways twists the torso (channel 34) against the lateral speed.
    void strafeTwist(int32_t frames) {
        float forward = speed04, side = lateral10;
        if (forward < 0.0f) forward = -forward;
        if (side < 0.0f) side = -side;
        int32_t target = 0;
        if (side < forward) target = -int32_t(data_->math.arctanf(lateral10, forward));
        for (int32_t i = 0; i < frames; ++i) twist580 = twist580 + shiftRight(diffAngle(twist580, target), 4);
        addTurn(0x44, int16_t(twist580));
    }
    // boyCanFire (overlay 16) for the ported moves: no skid, no half-turn,
    // not move 8, and a state that holds the weapon out, or crouched on moves
    // 0xE/0x21 once the model's clip blend (+0x5E) has ended.
    bool canFire() const {
        if (skid576 != 0 || halfTurn13E != 0 || move3B == 8) return false;
        const unsigned state = state568 & ~0x20u;
        if (state == 0 || state == 5 || state == 0xB || state == 0xA || (state == 3 && hover14A != 0)) return true;
        return (move3B == 0xE || move3B == 0x21) && blend5E == 0;
    }
    // Walking state 0: func_overlay_16_01002708.
    void walk(float speed, int16_t direction, int32_t frames, bool jumpPressed, int32_t stickX, int32_t stickY, float scale,
              const ControlModeKeys &mode) {
        const auto &math = data_->math;
        const auto &d = *data_;
        if (skid576) {
            skid576 = int8_t(skid576 - frames);
            if (skid576 < 0) skid576 = 0;
        }
        if (move3B == 0x19 && landingLock56B == 0) requestMove(chooseMove(), 0.0f);
        if (controlKeys & Pad::R) {  // standing aim
            beginAim(0xB);
            return;
        }
        if (5.0f < speed) speed = 5.0f;  // controlWalkingBack without aim locks
        strafe(0, frames, mode);
        if (jumpPressed && ceiling534 == 0) {
            const float current = magnitude(speed04);
            if (walkingBack569) { jumpDelay57A = 7; jumpCharge57B = 0; requestMove(7, 0.0f); }
            else if ((current > 1.25f && speed != 0.0f) || strafing56C) {
                jumpDelay57A = 0;
                velocity.y = velocity.y + 10.0f;
                requestMove(6, 0.0f);
            } else {
                jumpDelay57A = 0x18; jumpCharge57B = 0;
                requestMove(5, 0.0f);
            }
            state568 = 3; jumpReleased58E = 0; fallStart57C = position.y;
        } else if (controlDkeys & mode.crouch()) {  // unk189 < 4 for Juno
            if (magnitude(speed04) > 2.0f) { state568 = 1; requestMove(0xF, 0.0f); skid576 = 0x28; }  // slide
            else if (speed > 0.5f) { state568 = 2; requestMove(4, 0.0f); }                        // crouch-walk
            else { state568 = 1; requestMove(0xD, 0.0f); }                                          // crouch down
            strafing56C = 0;
        }
        if (controlKeys & mode.fire()) firing1F4 = 0xF;
        if (!grounded()) firing1F4 = 0;
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
        // Idle choice: +0x1F4 selects move 0x12 without drawing; at the end of an
        // idle clip that branch also restarts the move (+0x3B = -1).
        auto idle = [&](bool restart) {
            if (firing1F4 != 0) { next = 0x12; if (restart) forcedMove_ = 0x12; }
            else next = uint32_t(random_.next(0x10, 0x13));
            start = 0.0f;
        };
        auto strafe = [&]() { next = lateral10 < 0.0f ? 9 : 0xA; start = 0.25f; };
        switch (move3B) {
            case 0: case 28: case 36:
                rate = rate * largest;
                if (walkingBack569) next = 3;
                else if (largest < t[0]) idle(false);
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
                if (largest < t[1] && stickSpeed <= speed04) idle(false);
                else if (forward < side) strafe();
                else if (!walkingBack569) next = 0;
                break;
            case 4: case 44:  // crouch-walk
                rate = rate * largest;
                if (largest < t[2]) { start = 0.0f; next = 0x16; }
                break;
            case 5:
                if ((progress28 + (rate * dt)) > 0.75f) { progress28 = 0.75f; rate = 0.0f; }
                break;
            case 8:  // stand up
                if (progress28 == 1.0f) { next = chooseMove(); start = 0.0f; }
                break;
            case 11: if (roll584 != 2) { start = 0.0f; next = 0xE; } break;   // crouched rolls
            case 49: if (roll584 != 1) { start = 0.0f; next = 0xE; } break;
            case 12:                                                          // crouch-walk rolls
                if (roll584 != 4) { next = 4; start = 0.0f; if (largest < t[4]) next = 0x16; }
                break;
            case 50:
                if (roll584 != 3) { next = 4; start = 0.0f; if (largest < t[5]) next = 0x16; }
                break;
            case 13:  // crouch down; R doubles its rate
                start = 0.0f;
                if (controlKeys & Pad::R) rate = rate * 2.0f;
                if (t[6] < progress28) next = 0xE;
                break;
            case 14: case 33:  // crouched
                if (t[7] < largest) { start = 0.0f; next = 4; }
                break;
            case 22:  // crouch-walk stop
                if (t[10] < largest) { start = 0.0f; next = 4; }
                break;
            case 9: case 10: case 31: case 32: case 47: case 48:
                rate = rate * side;
                if (largest < t[3] && strafing56C == 0) idle(false);
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
                if ((progress28 + (rate * dt)) > 1.0f) idle(true);
                if (t[8] < largest) { next = 3; start = 0.25f; if (!walkingBack569) next = 0; }
                break;
            case 6: case 7: case 24: case 25:
                break;
            default:
                throw NotPortedError("Juno move outside the ported move-machine subset", "Este movimento do Juno ainda não foi portado.");
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
        if (outside)
            throw NotPortedError("Juno left the track bounds; controlRestartPlayer is not ported",
                                 "O Juno saiu dos limites da fase; o reinício do jogo original ainda não foi portado.");
        position = {nx, position.y + delta.y, position.z + delta.z};
    }
    // controlGroundHits for the five Juno spheres without object hit models.
    uint8_t groundHits(int32_t frames) {
        placeSpheres(frames);
        std::array<Vec3f, 5> ends{}, offsets{};
        std::array<float, 5> radii{};
        std::array<uint16_t, 5> flags{};
        uint8_t skip = skip531;
        for (size_t i = 0; i < 5; ++i, skip >>= 1) {
            ends[i] = sphereCurrent3C4[i];
            radii[i] = sphereDef(i).radius;
            flags[i] = sphereDef(i).flags;
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

// gen_anim_data (func_80074D60) with the player's list: each command-0 entry
// adds its value to one decoded s16 channel before the bone matrices. The
// channels are 3 per animated bone (60 for Juno's 20), and all 52 of Juno's
// clips map bone i to channel triple i (checked by movement_assets.py), so
// byte offset o turns bone o/6 about axis (o/2)%3. The same list applies to
// both clips of a blend, so adding it to the blended pose gives the same angle.
inline void applyJointTurns(Keyframe &pose, const JunoBody &body) {
    if (body.jointTurnCount > body.jointTurns.size()) throw std::runtime_error("Invalid Juno joint-turn list");
    for (size_t i = 0; i < body.jointTurnCount; ++i) {
        const auto &turn = body.jointTurns[i];
        if ((turn.offset & 0xF000) != 0 || (turn.offset & 1) || turn.offset >= 120 || size_t(turn.offset / 6) >= pose.angles.size())
            throw std::runtime_error("Juno joint turn outside the animated channels");
        if (turn.value == 0) continue;
        pose.angles[turn.offset / 6][(turn.offset / 2) % 3] += float(turn.value) * (2 * Pi / 65536);
    }
}
}
