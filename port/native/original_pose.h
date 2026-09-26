#pragma once
#include "animation.h"
#include "original_math.h"
#include <vector>

namespace jfg_native {

// Juno's pose as the original computes it: the model instance's two clip
// slots (objAnimSetMove, modGenAnimMatrices, the streamed clip cache
// func_8003CD70) and gen_anim_data (src/hasm/gen_anim_data.s), which decodes
// the packed clip streams, applies the joint-turn list, builds the local bone
// matrices (Euler from the sine table, or a blend of half-angle quaternions
// while the clip blend counter runs) and chains them under the object matrix.
// Values stay in single precision with the original operation order.

// One entry of the player's joint-turn list (controlPlayerTiltList, player
// +0x240), in the command format of gen_anim_data (func_80074D60). Command 0,
// the only one the ported states write, adds `value` to the s16 channel at
// byte offset `offset` of the decoded animation channels.
struct JointTurn { uint16_t offset = 0; int16_t value = 0; };
// MIPS sra on a 32-bit value (floor division by 2^n), without relying on
// implementation-defined signed shifts.
inline int32_t shiftRight(int32_t value, unsigned n) { return value >= 0 ? value >> n : ~(~value >> n); }

// A packed clip stream (asset 0x2B) as the clip cache holds it: byte 1 flags
// (0x10 loops, low nibble blend steps), +2 offset of the frame packets, and
// at +8 the header gen_anim_data reads (root bases and widths, frame count,
// packet stride, one 16-bit descriptor per channel).
struct AnimationStream {
    uint16_t animation = 0;
    std::vector<uint8_t> raw;
    uint8_t flags() const { return raw[1]; }
    uint32_t packets() const { return uint32_t(raw[2]) << 8 | raw[3]; }
    uint8_t frames() const { return raw[11]; }
    uint8_t stride() const { return raw[13]; }
    uint16_t descriptor(size_t channel) const { return uint16_t(raw[16 + channel * 2] << 8 | raw[17 + channel * 2]); }
};
// Model bone entry (+0x54): parent, matrix slot and offset. Bytes 2/3 hold
// the channel triple of each slot's clip; modGenAnimMatrices rewrites them
// from the per-clip maps (+0x58), kept here in `maps`.
struct AnimationBone { int8_t parent = -1; uint8_t slot = 0; std::array<float, 3> offset{}; };
struct JunoAnimationData {
    std::vector<AnimationStream> moves;                 // by move index (+0x5C animation ids)
    std::array<std::array<uint8_t, 21>, 52> maps{};     // channel triple per bone, per move
    std::array<AnimationBone, 21> bones{};
};

// The model instance fields gen_anim_data works from. Slot 0 is the current
// clip, slot 1 the clip being blended out: move (+0x24/+0x26), frame
// (+0x28/+0x2C), frame scale (+0x38/+0x3C), flags (+0x8/+0x9), cached stream
// (+0x80/+0x84) and the id it was loaded for (+0x88/+0x8A), then the packet
// (+0x50/+0x54) and next-packet offset (+0x58/+0x5A) of the frame to draw.
struct AnimationSlot {
    int32_t move = 0, loaded = -1, stream = -1, packet = 0, next = 0;
    float frame = 0, scale = 0;
    uint8_t flags = 0;
};
class AnimationInstance {
    static const AnimationStream &stream(const JunoAnimationData &data, int32_t move) {
        if (move < 0 || size_t(move) >= data.moves.size()) throw std::runtime_error("Juno clip slot outside the model");
        return data.moves[size_t(move)];
    }
    void header(size_t s, const AnimationStream &clip) {
        slots[s].flags = uint8_t(clip.flags() & 0xF0);
        slots[s].scale = float(clip.frames());
        if (slots[s].flags == 0) slots[s].scale = slots[s].scale - 1.0f;
    }
public:
    std::array<AnimationSlot, 2> slots{};
    int16_t counter5E = 0, step5C = 0;
    // func_8003CB50 for a streamed model: both slots cache move 0's stream,
    // recorded under its animation id rather than the move, so the first
    // modGenAnimMatrices reloads them.
    void init(const JunoAnimationData &data) {
        slots = {};
        counter5E = step5C = 0;
        const auto &first = stream(data, 0);
        slots[0].stream = slots[1].stream = 0;
        slots[0].loaded = slots[1].loaded = first.animation;
        header(0, first);
        slots[1].flags = slots[0].flags; slots[1].move = slots[0].move; slots[1].frame = slots[0].frame; slots[1].scale = slots[0].scale;
    }
    // objAnimSetMove, streamed path: slot 0 moves to slot 1; the new clip is
    // loaded by the next modGenAnimMatrices (the caller stores the progress).
    int32_t setMove(int32_t move, const JunoAnimationData &data) {
        slots[1].move = slots[0].move; slots[1].frame = slots[0].frame; slots[1].scale = slots[0].scale;
        slots[1].flags = slots[0].flags;
        const auto count = int32_t(data.moves.size());
        if (move >= count) move = count - 1;
        if (move < 0) move = 0;
        slots[0].move = move;
        slots[1].loaded = slots[0].loaded; slots[1].stream = slots[0].stream;
        slots[0].stream = -1;
        return move;
    }
    // modGenAnimMatrices up to gen_anim_data for the player: the slot 0 frame
    // from the object's progress with the scale loaded so far, the loads (a
    // stream with blend steps restarts the counter), the packets and the
    // counter step. Returns whether gen_anim_data blends.
    bool prepare(float progress, const JunoAnimationData &data) {
        int32_t used = 1;
        slots[0].frame = slots[0].scale * progress;
        if (counter5E != 0) used = 2;
        for (int32_t s = 0; s < used; ++s) {
            auto &slot = slots[size_t(s)];
            if (slot.stream != -1 && slot.loaded != slot.move) slot.stream = -1;
            if (slot.stream != -1) continue;
            const auto &clip = stream(data, slot.move);
            slot.stream = slot.move; slot.loaded = slot.move;
            header(size_t(s), clip);
            if (const int32_t steps = clip.flags() & 0xF) { counter5E = 0x3FF; used = 2; step5C = int16_t(0x3FF / steps); }
        }
        for (int32_t s = 0; s < used; ++s) {
            auto &slot = slots[size_t(s)];
            const auto &clip = stream(data, slot.stream);
            const int32_t whole = OriginalMath::truncate(slot.frame), stride = clip.stride();
            slot.next = float(whole) != slot.frame ? stride : 0;
            if (slot.flags != 0 && float(whole) == slot.scale - 1.0f) slot.next = -stride * whole;
            slot.packet = int32_t(clip.packets()) + stride * whole;
        }
        if (counter5E != 0) {
            counter5E = int16_t(counter5E - step5C);
            if (counter5E < 0) counter5E = 0;
        }
        return counter5E > 0;
    }
};

class OriginalPose {
    // gen_anim_data's buffers: decoded channels (D_800A798C) and channel
    // scales (D_800A7BCC). Slot 0 fills entries 0..59 and slot 1 60..119; the
    // unused triple 20 of Juno's last bone reads what the previous frames
    // left there, as in the original.
    std::array<uint16_t, 288> channels_{}, scales_{};
    std::array<Matrix, 21> world_{};
    const OriginalMath *math_ = nullptr;
    const AnimationStream *clip_ = nullptr;

    // MSB-first packet bits, as the 64-bit windows of func_80074B50 read them.
    // The original reads the heap past a stream when the frame scale left by
    // the previous clip points beyond it; the port reads zero bytes there.
    uint32_t bits(int32_t packet, uint32_t position, uint32_t width) {
        uint32_t value = 0;
        for (uint32_t i = 0; i < width; ++i) {
            const int64_t bit = int64_t(packet) * 8 + position + i, byte = bit >> 3;
            uint8_t data = 0;
            if (byte >= 0 && size_t(byte) < clip_->raw.size()) data = clip_->raw[size_t(byte)];
            else outside = true;
            value = (value << 1) | ((data >> (7 - (bit & 7))) & 1u);
        }
        return value;
    }
    // func_80074B50: one slot's root (x1024) and channels at its frame.
    void decode(const AnimationStream &clip, const AnimationSlot &slot, size_t base, const JointTurn *turns, size_t count,
                std::array<float, 3> &root) {
        clip_ = &clip; outside = false;
        const float whole = std::floor(slot.frame);
        const int32_t t9 = OriginalMath::roundHalfEven((slot.frame - whole) * 1024.0f);
        const int32_t a = slot.packet, b = slot.packet + slot.next;
        const uint8_t widths[3] = {uint8_t(clip.raw[14] >> 4), uint8_t(clip.raw[14] & 15), uint8_t(clip.raw[15] & 15)};
        uint32_t position = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            uint32_t value = uint32_t(int32_t(int8_t(clip.raw[8 + axis * 2]))) << 11;
            if (const uint32_t width = widths[axis]) {
                const uint32_t from = bits(a, position, width), to = bits(b, position, width);
                position += width;
                value = value + ((from << 10) + uint32_t(int32_t(to - from) * t9));
            }
            root[axis] = float(int32_t(value));
        }
        for (size_t channel = 0; channel < 60; ++channel) {
            const uint16_t d = clip.descriptor(channel);
            if (d & 0x10) throw std::runtime_error("Scale channels are outside Juno's clips");
            uint32_t value = d & 0xFFF0u;
            if (const uint32_t width = d & 15u) {
                const uint32_t from = bits(a, position, width), to = bits(b, position, width);
                position += width;
                int32_t difference = int32_t((to - from) << 21);
                difference = shiftRight(difference, 21);
                value = value + ((from + uint32_t(shiftRight(difference * t9, 10))) << 5);
            }
            channels_[base + channel] = uint16_t(value);
            scales_[base + channel] = 0;
        }
        if (outside) ++outsideReads;
        // func_80074D60: the joint-turn list on this slot's channels.
        for (size_t i = 0; i < count; ++i) {
            if ((turns[i].offset & 0xF000) != 0 || (turns[i].offset & 1) || base + turns[i].offset / 2 >= channels_.size())
                throw std::runtime_error("Juno joint turn outside the channel buffer");
            auto &channel = channels_[base + turns[i].offset / 2];
            channel = uint16_t(int32_t(turns[i].value) + int16_t(channel));
        }
    }
    // Inline lookups of gen_anim_data. Full angle: bits 4..13 index the
    // quarter-wave table, bits 14..15 pick the quadrant (Euler path).
    void full(uint16_t angle, float &sine, float &cosine) const {
        const auto &table = math_->sineTable();
        const uint32_t index = (angle >> 4) & 0x3FF, quadrant = (angle >> 4) & 0xC00;
        const float low = table[index], high = table[0x400 - index];
        if (quadrant == 0xC00) { cosine = low; sine = 0.0f - high; }
        else if (quadrant == 0x800) { sine = 0.0f - low; cosine = 0.0f - high; }
        else if (quadrant == 0x400) { cosine = 0.0f - low; sine = high; }
        else { sine = low; cosine = high; }
    }
    // Half angle: bits 5..14 index the table, bit 15 the second half (quaternions).
    void half(int16_t angle, float &sine, float &cosine) const {
        const auto &table = math_->sineTable();
        const uint32_t index = (uint32_t(int32_t(angle)) >> 5) & 0x3FF;
        if (angle < 0) { cosine = 0.0f - table[index]; sine = table[0x400 - index]; }
        else { sine = table[index]; cosine = table[0x400 - index]; }
    }
    // func_80074A68 and its caller: the quaternion (w, x, y, z) of a channel triple.
    std::array<float, 4> quaternion(size_t at) const {
        float c0, s0, c1, s1, c2, s2;
        half(int16_t(channels_[at]), s0, c0);
        half(int16_t(channels_[at + 1]), s1, c1);
        const float f6 = c0 * c1, f7 = c0 * s1, f8 = s0 * c1, f9 = s0 * s1;
        half(int16_t(channels_[at + 2]), s2, c2);
        const float f10 = f6 * c2, f11 = f9 * s2, f12 = f8 * c2, f13 = f7 * s2;
        const float f14 = f7 * c2, f15 = f8 * s2, f16 = f6 * s2, f17 = f9 * c2;
        return {f10 + f11, f12 - f13, f14 + f15, f16 - f17};
    }
    float scaleOf(size_t at) const { return float(int32_t(scales_[at])) * 0.000030517578f; }
public:
    uint32_t outsideReads = 0;  // frames whose packets reached past a stream
    bool outside = false;
    const std::array<Matrix, 21> &world() const { return world_; }
    uint16_t channel(size_t i) const { return channels_.at(i); }
    const std::array<Matrix, 21> &compute(const AnimationInstance &instance, const JunoAnimationData &data, const OriginalMath &math,
                                          const Matrix &object, const JointTurn *turns, size_t turnCount) {
        math_ = &math;
        const auto &first = data.moves.at(size_t(instance.slots[0].stream));
        if (data.bones.size() != 21) throw std::runtime_error("Juno bone table differs");
        std::array<float, 3> root{};
        decode(first, instance.slots[0], 0, turns, turnCount, root);
        std::array<Matrix, 21> local{};
        const auto &mapA = data.maps.at(size_t(instance.slots[0].move));
        if (instance.counter5E <= 0) {
            for (size_t i = 0; i < 21; ++i) {
                const auto &bone = data.bones[i];
                const size_t at = size_t(mapA[i]) * 3;
                float sx, cx, sy, cy, sz, cz;
                full(channels_[at], sx, cx);
                full(channels_[at + 2], sz, cz);
                const float f6 = sx * sz, f7 = cx * sz, f8 = sx * cz, f9 = cx * cz;
                full(channels_[at + 1], sy, cy);
                auto &m = local[bone.slot];
                m = {cy * cz, cy * sz, 0.0f - sy, 0, (f8 * sy) - f7, (f6 * sy) + f9, sx * cy, 0,
                     (f9 * sy) + f6, (f7 * sy) - f8, cx * cy, 0, bone.offset[0], bone.offset[1], bone.offset[2], 1};
                for (unsigned row = 0; row < 3; ++row)
                    if (scales_[at + row]) for (unsigned col = 0; col < 3; ++col) m[row * 4 + col] = m[row * 4 + col] * scaleOf(at + row);
            }
        } else {
            std::array<float, 3> rootB{};
            const auto &second = data.moves.at(size_t(instance.slots[1].stream));
            decode(second, instance.slots[1], 60, turns, turnCount, rootB);
            const auto &mapB = data.maps.at(size_t(instance.slots[1].move));
            const int32_t counter = instance.counter5E;
            const float w = float(counter) / 1024.0f, keep = 1.0f - w;
            for (unsigned axis = 0; axis < 3; ++axis) root[axis] = ((root[axis] - rootB[axis]) * keep) + rootB[axis];
            // Channel scales: slot 1's x scale (+2) against slot 0's, weighted
            // by the counter; zero reads as 0x8000.
            for (size_t i = 0; i < 21; ++i) {
                const size_t to = size_t(mapA[i]) * 3, from = 60 + size_t(mapB[i]) * 3;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    uint32_t other = scales_[from];
                    other = other ? other + 2 : 0x8002;
                    uint32_t mine = scales_[to + axis];
                    if (!mine) mine = 0x8000;
                    scales_[to + axis] = uint16_t(uint32_t(shiftRight(int32_t(other - mine) * counter, 10)) + mine);
                }
            }
            for (size_t i = 0; i < 21; ++i) {
                const auto &bone = data.bones[i];
                const size_t at = size_t(mapA[i]) * 3;
                auto q1 = quaternion(at), q2 = quaternion(60 + size_t(mapB[i]) * 3);
                float dot = q1[0] * q2[0];
                dot = dot + (q1[1] * q2[1]);
                dot = dot + (q1[2] * q2[2]);
                dot = dot + (q1[3] * q2[3]);
                for (auto &v : q1) v = v * keep;
                if (dot < 0.0f) for (auto &v : q2) v = 0.0f - v;
                for (auto &v : q2) v = v * w;
                const float qw = q1[0] + q2[0], qx = q1[1] + q2[1], qy = q1[2] + q2[2], qz = q1[3] + q2[3];
                const float x2 = qx * 2.0f, y2 = qy * 2.0f, z2 = qz * 2.0f;
                const float wx = qw * x2, wy = qw * y2, wz = qw * z2, xx = qx * x2, xy = qx * y2, xz = qx * z2;
                const float zz = qz * z2, yy = qy * y2, yz = qy * z2;
                auto &m = local[bone.slot];
                m = {1.0f - (yy + zz), xy + wz, xz - wy, 0, xy - wz, 1.0f - (xx + zz), yz + wx, 0,
                     xz + wy, yz - wx, 1.0f - (xx + yy), 0, bone.offset[0], bone.offset[1], bone.offset[2], 1};
                for (unsigned row = 0; row < 3; ++row)
                    if (scales_[at + row]) for (unsigned col = 0; col < 3; ++col) m[row * 4 + col] = m[row * 4 + col] * scaleOf(at + row);
            }
        }
        // func_8007524C: the root (1/1024 units) through the object matrix,
        // then each bone under its parent (the per-bone offsets of D_800A7E0C
        // are never set).
        Matrix parent = object;
        const float rx = root[0] * 0.0009765625f, ry = root[1] * 0.0009765625f, rz = root[2] * 0.0009765625f;
        parent[12] = parent[12] + (((rx * object[0]) + (ry * object[4])) + (rz * object[8]));
        parent[13] = parent[13] + (((rx * object[1]) + (ry * object[5])) + (rz * object[9]));
        parent[14] = parent[14] + (((rx * object[2]) + (ry * object[6])) + (rz * object[10]));
        for (size_t i = 0; i < 21; ++i) {
            const auto &bone = data.bones[i];
            const Matrix &p = i == 0 ? parent : world_.at(size_t(uint8_t(bone.parent)));
            const Matrix &l = local[bone.slot];
            Matrix w{};
            w[0] = ((l[0] * p[0]) + (l[1] * p[4])) + (l[2] * p[8]);
            w[1] = ((l[2] * p[9]) + (l[0] * p[1])) + (l[1] * p[5]);
            w[2] = ((l[0] * p[2]) + (l[1] * p[6])) + (l[2] * p[10]);
            w[4] = ((l[4] * p[0]) + (l[5] * p[4])) + (l[6] * p[8]);
            w[5] = ((l[6] * p[9]) + (l[4] * p[1])) + (l[5] * p[5]);
            w[6] = ((l[4] * p[2]) + (l[5] * p[6])) + (l[6] * p[10]);
            w[8] = ((l[8] * p[0]) + (l[9] * p[4])) + (l[10] * p[8]);
            w[9] = ((l[10] * p[9]) + (l[8] * p[1])) + (l[9] * p[5]);
            w[10] = ((l[8] * p[2]) + (l[9] * p[6])) + (l[10] * p[10]);
            w[12] = (((l[12] * p[0]) + p[12]) + (l[13] * p[4])) + (l[14] * p[8]);
            w[13] = (((l[12] * p[1]) + p[13]) + (l[13] * p[5])) + (l[14] * p[9]);
            w[14] = (((l[12] * p[2]) + p[14]) + (l[13] * p[6])) + (l[14] * p[10]);
            w[15] = 1;
            for (float v : w) if (!std::isfinite(v)) throw std::runtime_error("Nonfinite original bone matrix");
            world_[bone.slot] = w;
        }
        return world_;
    }
};

// matrix_SCL_RPY_XYZ: the object matrix from yaw (+0), pitch (+2), roll (+4)
// through Sinf/Cosf, the object scale (+8) and the position (+0xC).
inline Matrix objectMatrix(const OriginalMath &math, const std::array<int16_t, 3> &angles, float scale, const Vec3f &position) {
    const float sy = math.sinf(angles[0]), cy = math.cosf(angles[0]);
    const float sp = math.sinf(angles[1]), cp = math.cosf(angles[1]);
    const float sr = math.sinf(angles[2]), cr = math.cosf(angles[2]);
    const float f8 = sr * sy, f9 = sr * cy, f10 = sy * cr, f11 = cr * cy, f12 = sr * sy;
    const float f13 = sr * cp, f14 = cr * cp, f15 = sy * cp, f16 = -sp, f17 = cp * cy;
    return {((f8 * sp) + f11) * scale, f13 * scale, ((f9 * sp) - f10) * scale, 0,
            ((f10 * sp) - f9) * scale, f14 * scale, ((f11 * sp) + f12) * scale, 0,
            f15 * scale, f16 * scale, f17 * scale, 0,
            position.x, position.y, position.z, 1};
}
}
