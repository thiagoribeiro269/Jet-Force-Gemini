#pragma once
#include "animation.h"

namespace jfg_native {
inline Vec3 subtract(const Vec3 &a, const Vec3 &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline float dot(const Vec3 &a, const Vec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline Vec3 normalized(const Vec3 &v) {
    const float length = std::sqrt(dot(v, v));
    if (!std::isfinite(length) || length < 1e-6f) throw std::runtime_error("Degenerate native camera axis");
    return {v[0] / length, v[1] / length, v[2] / length};
}
inline Matrix identityMatrix() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
inline Matrix uniformScale(float scale) { return {scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, 1}; }
struct WorldCamera {
    Vec3 eye{}, target{}, up{0, 1, 0};
    float verticalFov = 55 * Pi / 180, aspect = 4.0f / 3, nearPlane = 3, farPlane = 12000;
    Vec3 clearColor{0.22f, 0.35f, 0.46f}; // Controlled backdrop, not the original sky.
    Matrix matrix() const {
        for (const auto &v : {eye, target, up}) for (float value : v)
            if (!std::isfinite(value) || std::abs(value) > 1000000) throw std::runtime_error("Invalid native camera vector");
        for (float value : clearColor) if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Invalid native clear color");
        if (!std::isfinite(verticalFov) || verticalFov <= 0 || verticalFov >= Pi || !std::isfinite(aspect) || aspect <= 0 ||
            !std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane)
            throw std::runtime_error("Invalid native perspective frustum");
        // Right-handed world and row-vector matrices; D3D depth interval [0,1].
        const auto backward = normalized(subtract(eye, target));
        const auto right = normalized(cross(up, backward));
        const auto vertical = cross(backward, right);
        const Matrix view{right[0], vertical[0], backward[0], 0,
                          right[1], vertical[1], backward[1], 0,
                          right[2], vertical[2], backward[2], 0,
                          -dot(eye, right), -dot(eye, vertical), -dot(eye, backward), 1};
        const float y = 1 / std::tan(verticalFov / 2), z = farPlane / (nearPlane - farPlane);
        const Matrix projection{y / aspect, 0, 0, 0, 0, y, 0, 0, 0, 0, z, -1, 0, 0, nearPlane * z, 0};
        return multiply(view, projection);
    }
};
}
