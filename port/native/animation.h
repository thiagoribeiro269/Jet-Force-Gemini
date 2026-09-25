#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace jfg_native {
using Vec3 = std::array<float, 3>;
using Matrix = std::array<float, 16>;
struct Bone { int32_t parent; Vec3 offset; };
struct Keyframe { Vec3 root; std::vector<Vec3> angles; };
struct Clip { uint32_t id = 0; bool loop = false; float sourceRate = 15; std::vector<Keyframe> keys; };
constexpr float Pi = 3.14159265358979323846f;

inline Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix result{};
    for (unsigned row = 0; row < 4; ++row) for (unsigned col = 0; col < 4; ++col)
        for (unsigned k = 0; k < 4; ++k) result[row * 4 + col] += a[row * 4 + k] * b[k * 4 + col];
    return result;
}

// Row-vector convention: local X, then Y, then Z rotation and translation.
inline Matrix localMatrix(const Vec3 &angles, const Vec3 &translation) {
    const float sx = std::sin(angles[0]), cx = std::cos(angles[0]);
    const float sy = std::sin(angles[1]), cy = std::cos(angles[1]);
    const float sz = std::sin(angles[2]), cz = std::cos(angles[2]);
    return {cy * cz, cy * sz, -sy, 0,
            sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy, 0,
            cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy, 0,
            translation[0], translation[1], translation[2], 1};
}

inline Vec3 transform(const Vec3 &p, const Matrix &m) {
    return {p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12],
            p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
            p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]};
}

inline float interpolateAngle(float from, float to, float fraction) {
    return from + std::remainder(to - from, 2 * Pi) * fraction;
}

inline Keyframe sampleClip(const Clip *clip, size_t boneCount, float frame) {
    if (boneCount == 0 || boneCount > 64 || !std::isfinite(frame) || frame < 0)
        throw std::runtime_error("Invalid native pose input");
    Keyframe result{{0, 0, 0}, std::vector<Vec3>(boneCount)};
    size_t first = 0, second = 0;
    float fraction = 0;
    if (clip) {
        if (clip->keys.empty()) throw std::runtime_error("Empty animation clip");
        const float count = float(clip->keys.size());
        frame = clip->loop ? std::fmod(frame, count) : std::fmin(frame, count - 1);
        first = size_t(std::floor(frame));
        second = first + 1 < clip->keys.size() ? first + 1 : clip->loop ? 0 : first;
        fraction = frame - float(first);
        if (clip->keys[first].angles.size() != boneCount || clip->keys[second].angles.size() != boneCount)
            throw std::runtime_error("Animation/skeleton channel mismatch");
        for (unsigned axis = 0; axis < 3; ++axis) {
            result.root[axis] = clip->keys[first].root[axis] * (1 - fraction) + clip->keys[second].root[axis] * fraction;
            for (size_t bone = 0; bone < boneCount; ++bone)
                result.angles[bone][axis] = interpolateAngle(clip->keys[first].angles[bone][axis], clip->keys[second].angles[bone][axis], fraction);
        }
    }
    return result;
}

inline Keyframe mixPoses(const Keyframe &from, const Keyframe &to, float weight) {
    if (from.angles.empty() || from.angles.size() != to.angles.size() || !std::isfinite(weight) || weight < 0 || weight > 1)
        throw std::runtime_error("Invalid local-pose blend");
    // Copy exact endpoints, including their angular representation.
    if (weight == 0) return from;
    if (weight == 1) return to;
    Keyframe result{{0, 0, 0}, std::vector<Vec3>(from.angles.size())};
    for (unsigned axis = 0; axis < 3; ++axis) {
        result.root[axis] = from.root[axis] * (1 - weight) + to.root[axis] * weight;
        for (size_t bone = 0; bone < from.angles.size(); ++bone)
            result.angles[bone][axis] = interpolateAngle(from.angles[bone][axis], to.angles[bone][axis], weight);
    }
    return result;
}

inline std::vector<Matrix> composePose(const std::vector<Bone> &bones, const Keyframe &local) {
    if (bones.empty() || bones.size() > 64 || local.angles.size() != bones.size())
        throw std::runtime_error("Invalid native skeleton/pose size");
    std::vector<Matrix> matrices;
    for (size_t bone = 0; bone < bones.size(); ++bone) {
        const auto &node = bones[bone];
        if (node.parent < -1 || node.parent >= int32_t(bone) || (node.parent == -1) != (bone == 0))
            throw std::runtime_error("Invalid skeleton parent order");
        Vec3 translation = node.offset;
        if (bone == 0) for (unsigned axis = 0; axis < 3; ++axis) translation[axis] += local.root[axis];
        auto matrix = localMatrix(local.angles[bone], translation);
        if (node.parent >= 0) matrix = multiply(matrix, matrices[size_t(node.parent)]);
        for (float value : matrix) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite bone matrix");
        matrices.push_back(matrix);
    }
    return matrices;
}

inline std::vector<Matrix> pose(const std::vector<Bone> &bones, const Clip *clip, float frame) {
    return composePose(bones, sampleClip(clip, bones.size(), frame));
}
}
