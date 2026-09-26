#pragma once
#include "juno_body.h"
#include "camera.h"

namespace jfg_native {

// Original free camera for player type 0, ported from charControl:
// func_8002B378 (dispatcher, default path), func_8002CF78 (free camera),
// func_8002CBD0 (camera collision, mode 1), func_8002F0E8/func_8002F2BC
// (camera zones, registered only by OverrideCamera objects; Forest First has
// none, as the converter checks), func_8002F45C
// (per-character profile scaling) and func_8002EDA0 (initialization), plus
// camera.c camInit/func_8003F66C defaults and camSetProjMtx projection.
struct JunoCameraData {
    uint32_t level = 0, mode = 0;
    std::array<std::array<float, 4>, 8> characters{};   // 0x800A2BE0..0x800A2C5F
    std::array<std::array<float, 9>, 6> profiles{};     // 0x800A2C60, C84, CA8, CCC, CF0, D14
};

inline std::shared_ptr<const JunoCameraData> readJunoCamera(const char *path) {
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    size_t at = 0;
    auto take = [&](size_t count) { if (at + count > bytes.size()) throw std::runtime_error("Truncated Juno camera data");
                                     const auto *p = bytes.data() + at; at += count; return p; };
    auto u32 = [&]() { const auto *p = take(4); return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
    auto f32 = [&]() {
        const auto bits = u32(); float value; std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value)) throw std::runtime_error("Nonfinite Juno camera value");
        return value;
    };
    if (bytes.size() < 20 || std::memcmp(take(8), "JFGCAM1\0", 8)) throw std::runtime_error("Wrong Juno camera format");
    auto data = std::make_shared<JunoCameraData>();
    data->level = u32(); data->mode = u32();
    if (data->level != 21 || data->mode != 0 || u32() != 6) throw std::runtime_error("Unsupported Juno camera profile");
    for (auto &row : data->characters) for (auto &value : row) value = f32();
    for (auto &row : data->profiles) for (auto &value : row) value = f32();
    if (at != bytes.size()) throw std::runtime_error("Trailing Juno camera payload");
    if (data->profiles[0][3] != 150.0f || data->profiles[0][5] != 0.02f || data->characters[0][0] != 40.0f)
        throw std::runtime_error("Juno camera tables differ from the inspected profile");
    return data;
}

// Buttons read by the camera and aim code (controlKeys bits).
struct CameraKeys { bool cRight = false, cLeft = false, trigger = false; };
// func_8002CF78 reads the global controlKeys left by the character routine:
// 0x1 (C-right), 0x2 (C-left) and 0x10 (R). disablejoy has zeroed them.
inline CameraKeys cameraKeys(uint16_t controlKeys) {
    return {bool(controlKeys & Pad::CRight), bool(controlKeys & Pad::CLeft), bool(controlKeys & Pad::R)};
}

class JunoCamera {
public:
    // Camera actor (cameraActorArray entry, controlcam).
    std::array<int16_t, 3> angles{};   // +0x00 yaw, +0x02 pitch, +0x04 roll
    Vec3f position, previous;          // +0x0C, +0x18
    float fov = 60.0f;                 // +0x2C
    uint8_t mode49 = 1;
    // Player camera fields (+0x104..+0x118) and the per-player state D_800F6E58.
    int16_t orbit104 = 0, heading106 = 0, offset10A = 0;
    uint8_t turning108 = 0, turnTicks109 = 0, relaxed10C = 0, ready574 = 0;
    float near110 = 0, far114 = 0, lookAhead118 = 0;
    float zoneHeight8 = 0, zoneDistance0C = 0, distance10 = 1, scale14 = 1, aim18 = 0, hover1C = 0, blend20 = 0, fall24 = 1, blend28 = 0;

    JunoCamera(std::shared_ptr<const JunoCameraData> data, JunoBody &body) : data_(std::move(data)) {
        if (!data_) throw std::runtime_error("Missing Juno camera data");
        // camInit/func_8003F66C(200, 200, 200, 0, 0, 180), then controlPlayerInit.
        position = previous = {200.0f, 200.0f, 200.0f};
        angles = {int16_t(180 * 0xB6), 0, 0};
        orbit104 = int16_t(0x8000 - body.orientation[0]);
        // func_8002EDA0: reset, then eight immediate updates before enabling smoothing.
        angles = {0, 0, 0};
        position.y = body.position.y + 80.0f;
        previous = position;
        mode49 = 1;
        zoneHeight8 = zoneDistance0C = 0; distance10 = 1; scale14 = 1; aim18 = hover1C = blend20 = 0; fall24 = 1; blend28 = 0;
        ready574 = 0;
        for (int i = 0; i < 8; ++i) tick(body, {}, 1);
        ready574 = 1;
    }
    int16_t yaw() const { return angles[0]; }

    // func_8002B378 for a free-camera player (no cutscene, static, lobby or spline camera).
    void tick(JunoBody &body, const CameraKeys &keys, int32_t frames) {
        freeCamera(body, keys, frames);
        const auto &extents = body.query().track().extents;
        const float floor = float(extents[2]) - 100.0f;
        if (position.y < floor) position.y = position.y + ((floor - position.y) * 0.175f);
    }

    // Projection of camSetProjMtx: guPerspective(fov, 4/3, 10, 15000) with
    // the view looking from the camera angles (yaw/pitch) as the free camera
    // computes them; roll is driven to zero by the same routine.
    WorldCamera view() const {
        const auto &math = data_math();
        const float cp = math.cosf(angles[1]), sp = math.sinf(angles[1]);
        const Vec3f forward{-math.sinf(angles[0]) * cp, -sp, math.cosf(angles[0]) * cp};
        WorldCamera camera;
        camera.eye = {position.x, position.y, position.z};
        camera.target = {position.x + forward.x * 100.0f, position.y + forward.y * 100.0f, position.z + forward.z * 100.0f};
        camera.verticalFov = fov * (Pi / 180.0f);
        camera.aspect = 4.0f / 3.0f;
        camera.nearPlane = 10.0f;
        camera.farPlane = 15000.0f;
        return camera;
    }
    void bind(const JunoBody &body) { math_ = &body.data().math; }

private:
    std::shared_ptr<const JunoCameraData> data_;
    const OriginalMath *math_ = nullptr;
    const OriginalMath &data_math() const {
        if (!math_) throw std::runtime_error("Juno camera is not bound to a body");
        return *math_;
    }
    static float smooth(float value, float target, float rate, int32_t frames) {
        return ((1.0f - OriginalMath::powerf(rate, frames)) * (target - value)) + value;
    }
    static void blend(std::array<float, 9> &out, const std::array<float, 9> &profile, float k) {
        for (size_t i = 0; i < 9; ++i) out[i] = out[i] + ((profile[i] - out[i]) * k);
    }

    void freeCamera(JunoBody &body, const CameraKeys &keys, int32_t frames) {
        math_ = &body.data().math;
        const auto &math = *math_;
        const auto &d = *data_;
        const float speed = body.speed04;
        if (body.state568 == 2 && (speed < -0.5f || speed > 0.5f)) relaxed10C = 1;
        else if (speed < -0.75f || speed > 0.75f) relaxed10C = 1;
        bool rotated = false;
        const bool trigger = keys.trigger;
        const bool aimLock = false;  // +0x540 & 0xF: aim locks are not integrated
        if (aimLock || trigger) relaxed10C = 1;
        else if (keys.cRight) { offset10A = int16_t(offset10A - (frames << 8)); rotated = true; relaxed10C = 0; }
        else if (keys.cLeft) { offset10A = int16_t(offset10A + (frames << 8)); rotated = true; relaxed10C = 0; }
        if (offset10A >= 0x2001) offset10A = 0x2000;
        if (offset10A < -0x2000) offset10A = -0x2000;
        if (relaxed10C) {
            offset10A = int16_t(OriginalMath::truncate(float(offset10A) * (trigger ? 0.8f : 0.95f)));
            if (offset10A >= -0x1FF && offset10A < 0x200) offset10A = 0;
        }
        if (body.grounded()) fall24 = 1.0f;
        float rate = 1.0f, pitchRate = 1.0f;
        const uint8_t state = body.state568;
        if (state == 2 || state == 10) scale14 = smooth(scale14, 0.35f, 0.95f, frames);
        else if (state == 1) scale14 = smooth(scale14, 0.6f, 0.95f, frames);
        else scale14 = smooth(scale14, 1.0f, 0.875f, frames);
        if (ready574) rate = 1.0f - OriginalMath::powerf(0.98f, frames);
        if (!aimLock || state == 6 || state == 7 || state == 8) {
            aim18 = aim18 + ((0.0f - aim18) * rate);
            if (aim18 < 0.05f) aim18 = 0.0f;
        } else aim18 = aim18 + ((1.0f - aim18) * rate);
        if (!body.hover14A) { hover1C = smooth(hover1C, 0.0f, 0.875f, frames); if (hover1C < 0.01f) hover1C = 0.0f; }
        else hover1C = smooth(hover1C, 1.0f, 0.95f, frames);
        blend20 = smooth(blend20, 0.0f, 0.97f, frames); if (blend20 < 0.01f) blend20 = 0.0f;   // +0x19E is cleared by boyControl
        blend28 = smooth(blend28, 0.0f, 0.97f, frames); if (blend28 < 0.01f) blend28 = 0.0f;   // +0x1FB not integrated
        // func_8002F45C: per-character profile entries scaled by the state factor.
        auto base = d.profiles[0], aimProfile = d.profiles[1], hoverProfile = d.profiles[3];
        const size_t type = 0;
        base[1] = d.characters[4][type] * scale14;         // 0x800A2C20
        aimProfile[1] = d.characters[5][type] * scale14;   // 0x800A2C30
        hoverProfile[1] = d.characters[7][type] * scale14; // 0x800A2C50
        base[4] = d.characters[0][type] * scale14;         // 0x800A2BE0
        aimProfile[4] = d.characters[1][type] * scale14;   // 0x800A2BF0
        hoverProfile[4] = d.characters[3][type] * scale14; // 0x800A2C10
        fov = ((aim18 * (1.0f - hover1C)) * 8.0f) + 52.0f;
        auto p = base;
        if (aim18 != 0.0f) blend(p, aimProfile, aim18);
        if (hover1C != 0.0f) blend(p, hoverProfile, hover1C);
        if (blend20 != 0.0f) blend(p, d.profiles[4], blend20);
        if (blend28 != 0.0f) blend(p, d.profiles[5], blend28);
        float dx = position.x - body.position.x, dz = position.z - body.position.z;
        const float horizontal = std::sqrt((dx * dx) + (dz * dz));
        float nearFactor, farFactor;
        if (horizontal > 240.0f) { nearFactor = 1.0f; farFactor = 1.0f; }
        else if (horizontal > 220.0f) { nearFactor = 1.0f; farFactor = (horizontal - 220.0f) / 20.0f; }
        else if (horizontal > 200.0f) { farFactor = 0.0f; nearFactor = (horizontal - 200.0f) / 20.0f; }
        else if (horizontal > 115.0f) { nearFactor = 0.0f; farFactor = 0.0f; }
        else if (horizontal > 85.0f) { farFactor = 0.0f; nearFactor = (horizontal - 115.0f) / -30.0f; }
        else { nearFactor = 1.0f; farFactor = 0.0f; }
        near110 = near110 + ((nearFactor - near110) * (1.0f - OriginalMath::powerf(0.75f, frames)));
        far114 = far114 + ((farFactor - far114) * (1.0f - OriginalMath::powerf(0.75f, frames)));
        float height = p[4], distance = p[3];
        // func_8002F2BC without a camera zone object: offsets decay to zero.
        const bool zonePosition = false, zoneFollow = true;
        const float zoneRate = ready574 ? 1.0f - OriginalMath::powerf(0.9f, frames) : 1.0f;
        zoneHeight8 = zoneHeight8 + ((0.0f - zoneHeight8) * zoneRate);
        zoneDistance0C = zoneDistance0C + ((0.0f - zoneDistance0C) * zoneRate);
        height = height + zoneHeight8;
        distance = distance + zoneDistance0C;
        bool chase = true;
        if (!trigger && !aimLock && ready574 && !(far114 > 0.05f) && hover1C == 0.0f) {
            if (state == 6 || state == 7 || state == 8 || state == 12) chase = true;
            else if (offset10A == 0 && !rotated) chase = false;
            else chase = zoneFollow && !relaxed10C;
            if (!chase) orbit104 = int16_t(0x8000 - int32_t(math.arctanf(dx, dz)));
        }
        if (chase) {
            const float follow = (trigger && !aimLock) ? 0.125f : p[5];
            if (ready574) rate = 1.0f - OriginalMath::powerf(1.0f - follow, frames);
            int32_t target = 0x8000 - body.orientation[0];
            if (zoneFollow) target += offset10A;
            orbit104 = OriginalMath::dAngle(orbit104, int16_t(target), rate);
        }
        Vec3f goal = position;
        if (!zonePosition) {
            if (aim18 != 0.0f || (rotated && zoneFollow)) {
                distance = p[3];
                height = height + ((p[4] - height) * aim18);
            } else {
                distance = distance10 * (horizontal + ((p[3] - horizontal) * (1.0f - OriginalMath::powerf(1.0f - near110, frames))));
            }
            goal = {(math.sinf(orbit104) * distance) + body.position.x, body.position.y + height,
                    body.position.z - (math.cosf(orbit104) * distance)};
        }
        if (state == 8 || (state == 3 && aim18 == 0.0f))
            if (body.fallStart57C < body.position.y) goal.y = goal.y + (body.fallStart57C - body.position.y);
        mode49 = uint8_t(d.mode + 1);
        if (mode49 != 1) throw std::runtime_error("Camera collision mode 2 is not ported");
        if (!zonePosition) {
            Vec3f look = math.rotateRPY(body.orientation, {0.0f, 40.0f, 0.0f});
            look = {look.x + body.position.x, look.y + body.position.y, look.z + body.position.z};
            if (state == 8 || state == 6 || state == 7) look.y = look.y + 60.0f;
            collide(body, look, goal);
        }
        if (ready574) rate = 1.0f - OriginalMath::powerf(1.0f - p[8], frames);
        position = {((goal.x - position.x) * rate) + position.x, ((goal.y - position.y) * rate) + position.y,
                    ((goal.z - position.z) * rate) + position.z};
        previous = position;
        // Look-ahead offset while the heading stays steady.
        auto change = int16_t(body.heading11C - heading106);
        if (change < 0) change = int16_t(-change);
        if (turning108) {
            if (change < 0x500) turnTicks109 = uint8_t(turnTicks109 - 1); else turnTicks109 = 5;
            if (turnTicks109 == 0) turning108 = 0;
        } else {
            if (change >= 0x681) turnTicks109 = uint8_t(turnTicks109 - 1); else turnTicks109 = 5;
            if (turnTicks109 == 0) turning108 = 1;
        }
        heading106 = body.heading11C;
        if (turning108 || aimLock || state == 10) {
            if (ready574) rate = 1.0f - OriginalMath::powerf(0.75f, frames);
            lookAhead118 = lookAhead118 * rate;
        } else {
            if (ready574) rate = 1.0f - OriginalMath::powerf(0.995f, frames);
            const float target = body.walkingBack569 ? 50.0f : -50.0f;
            lookAhead118 = lookAhead118 + ((target - lookAhead118) * rate);
        }
        dx = position.x - body.position.x; dz = position.z - body.position.z;
        const float spread = std::sqrt((dx * dx) + (dz * dz));
        const float reach = spread < 90.0f ? 0.0f : spread < 140.0f ? (spread - 90.0f) / 50.0f : 1.0f;
        const auto aimPoint = math.rotateRPY(body.orientation, {p[0], p[1], p[2] + (lookAhead118 * reach)});
        dx = position.x - (aimPoint.x + body.position.x);
        float dy = position.y - (aimPoint.y + body.position.y);
        dz = position.z - (aimPoint.z + body.position.z);
        if (!body.grounded() && hover1C == 0.0f && aim18 == 0.0f && state != 4 && body.fallStart57C < body.position.y &&
            (body.position.y - body.fallStart57C) < 108.0f)
            fall24 = fall24 + ((1.0f - fall24) * (1.0f - OriginalMath::powerf(0.95f, frames)));
        else fall24 = fall24 * OriginalMath::powerf(0.95f, frames);
        dy = dy + ((body.position.y - body.fallStart57C) * fall24);
        const auto yawTarget = int16_t(0x8000 - int32_t(math.arctanf(dx, dz)));
        const auto pitchTarget = int16_t(math.arctanf(dy, std::sqrt((dx * dx) + (dz * dz))));
        if (ready574) {
            rate = 1.0f - OriginalMath::powerf(1.0f - p[6], frames);
            pitchRate = 1.0f - OriginalMath::powerf(1.0f - p[7], frames);
        }
        angles[0] = OriginalMath::dAngle(angles[0], yawTarget, rate);
        angles[1] = OriginalMath::dAngle(angles[1], pitchTarget, pitchRate);
        angles[2] = OriginalMath::dAngle(angles[2], 0, rate);
        // The camera keeps Juno at least 32 units away horizontally.
        dx = body.position.x - position.x; dz = body.position.z - position.z;
        float squared = (dx * dx) + (dz * dz);
        if (squared < 1024.0f) {
            if (squared != 0.0f) {
                squared = (32.0f / std::sqrt(squared)) - 1.0f;
                dx = dx * squared; dz = dz * squared;
            }
            body.pushByCamera(dx, dz);
        }
    }
    // func_8002CBD0: ray from the aim point to the goal, then 16-unit clearance
    // from ceilings and floors around the camera.
    void collide(const JunoBody &body, const Vec3f &look, Vec3f &goal) const {
        const float requestedY = goal.y;
        int outcome = 1;
        TrackQuery::NearestHit hit;
        if (body.query().nearestIntersection(look, goal, hit, 0, 0)) {
            outcome = 3;
            goal = hit.point;
        }
        const auto ceilings = body.query().cylinderHeights(goal.x, goal.z, -32000.0f, 32000.0f, 16.0f, 0xC00, true);
        for (size_t i = ceilings.size(); i-- > 0;) {
            const float h = ceilings[i].height;
            if ((h - 16.0f) < goal.y && goal.y < (h + 1.0f)) goal.y = h - 16.0f;
        }
        const auto floors = body.query().cylinderHeights(goal.x, goal.z, -32000.0f, 32000.0f, 16.0f, 0xC00, false);
        for (size_t i = floors.size(); i-- > 0;) {
            const float h = floors[i].height;
            if ((h - 1.0f) < goal.y && goal.y < (h + 16.0f)) goal.y = h + 16.0f;
        }
        if (outcome == 1 && goal.y < requestedY) goal.y = requestedY;
    }
};
}
