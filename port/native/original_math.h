#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace jfg_native {
struct Vec3f { float x = 0, y = 0, z = 0; };
inline bool operator==(const Vec3f &a, const Vec3f &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// Recovered math primitives used by the original collision and movement code.
// Static sources: src/hasm/ido/math_util.s (Sinf/Cosf, Arctanf, Powerf,
// mathOneFloatRPY, mathOneFloatYPR, mathXZInTri) and charControl dAngle.
// Values stay in IEEE single precision and keep the original operation order;
// build these files with -ffp-contract=off so no fused operations appear.
class OriginalMath {
    std::array<float, 1025> sine_{};
    std::array<int16_t, 1025> arctan_{};
public:
    OriginalMath() = default;
    OriginalMath(const std::array<float, 1025> &sine, const std::array<int16_t, 1025> &arctan) : sine_(sine), arctan_(arctan) {
        if (sine_[0] != 0 || sine_[1024] != 1 || arctan_[0] != 0 || arctan_[1024] != 0x2000)
            throw std::runtime_error("Invalid original trigonometric tables");
        for (size_t i = 1; i < sine_.size(); ++i)
            if (!(sine_[i] >= sine_[i - 1]) || !(arctan_[i] >= arctan_[i - 1])) throw std::runtime_error("Nonmonotonic original table");
    }
    // Sinf: quarter-wave table indexed by bits 4..13; the second quadrant
    // mirrors with one extra step when bits 0..3 are nonzero.
    float sinf(int32_t angle) const {
        const uint32_t a = uint32_t(angle);
        uint32_t index = (a >> 4) & 0x3FF;
        if (a & 0x4000) {
            if (a & 0xF) index += 1;
            index = 0x400 - index;
        }
        const float value = sine_[index];
        return (a & 0x8000) ? -value : value;
    }
    float cosf(int32_t angle) const { return sinf(int32_t(uint32_t(angle) + 0x4000)); }
    // Arctanf(y, x): binary angle 0..0xFFFF. cvt.w.s runs under the default
    // round-to-nearest-even mode; the result is masked to an even byte offset.
    uint16_t arctanf(float y, float x) const {
        uint32_t result = 0;
        if (y == 0.0f && x == 0.0f) return 0;
        if (y < 0.0f) {
            y = -y;
            if (x < 0.0f) { x = -x; result = 0x8000; }
            else { result = 0xC000; std::swap(x, y); }
        } else if (x < 0.0f) {
            x = -x; result = 0x4000; std::swap(x, y);
        }
        const bool direct = y < x;
        const float ratio = direct ? y / x : x / y;
        const auto index = size_t(uint32_t(roundHalfEven(ratio * 2048.0f)) & 0xFFE) >> 1;
        if (index >= arctan_.size()) throw std::runtime_error("Arctangent table index outside original range");
        result = direct ? result + uint32_t(int32_t(arctan_[index])) : result + 0x4000 - uint32_t(int32_t(arctan_[index]));
        return uint16_t(result & 0xFFFF);
    }
    static int32_t roundHalfEven(float value) {
        if (!std::isfinite(value) || std::abs(value) > 2147483520.0f) return 0x7FFFFFFF;
        const float floorValue = std::floor(value);
        const float fraction = value - floorValue;
        auto result = int32_t(floorValue);
        if (fraction > 0.5f || (fraction == 0.5f && (result & 1))) ++result;
        return result;
    }
    // Powerf: repeated multiplication/division from 1.0, one rounding per step.
    static float powerf(float base, int32_t exponent) {
        float result = 1.0f;
        if (exponent > 0) for (int32_t i = 0; i < exponent; ++i) result = result * base;
        else for (int32_t i = 0; i > exponent; --i) result = result / base;
        return result;
    }
    // dAngle: shortest 16-bit difference, scaled and truncated toward zero.
    static int16_t dAngle(int16_t current, int32_t target, float fraction) {
        int32_t difference = (target - current) & 0xFFFF;
        const int32_t opposite = (current - target) & 0xFFFF;
        if (opposite < difference) difference = -opposite;
        return int16_t(current + truncate(float(difference) * fraction));
    }
    // IDO float-to-int casts temporarily select round-toward-zero.
    static int32_t truncate(float value) {
        if (!std::isfinite(value) || std::abs(value) >= 2147483648.0f) return 0x7FFFFFFF;
        return int32_t(value);
    }
    // mathOneFloatRPY with angles {a0,a1,a2}: rotate by a2 (XY), a1 (YZ), a0 (XZ).
    Vec3f rotateRPY(const std::array<int16_t, 3> &angles, Vec3f v) const {
        float s = sinf(angles[2]), t3 = v.x * s, fa0 = v.y * s, c = cosf(angles[2]);
        v.x = v.x * c; v.x = v.x - fa0; v.y = v.y * c; v.y = v.y + t3;
        s = sinf(angles[1]); t3 = v.y * s; fa0 = v.z * s; c = cosf(angles[1]);
        v.y = v.y * c; v.y = v.y - fa0; v.z = v.z * c; v.z = v.z + t3;
        s = sinf(angles[0]); t3 = v.x * s; fa0 = v.z * s; c = cosf(angles[0]);
        v.x = v.x * c; v.x = v.x + fa0; v.z = v.z * c; v.z = v.z - t3;
        return v;
    }
    // mathOneFloatYPR: the reverse order, a0 (XZ), a1 (YZ), a2 (XY).
    Vec3f rotateYPR(const std::array<int16_t, 3> &angles, Vec3f v) const {
        float s = sinf(angles[0]), t3 = v.x * s, fa0 = v.z * s, c = cosf(angles[0]);
        v.x = v.x * c; v.x = v.x + fa0; v.z = v.z * c; v.z = v.z - t3;
        s = sinf(angles[1]); t3 = v.y * s; fa0 = v.z * s; c = cosf(angles[1]);
        v.y = v.y * c; v.y = v.y - fa0; v.z = v.z * c; v.z = v.z + t3;
        s = sinf(angles[2]); t3 = v.x * s; fa0 = v.y * s; c = cosf(angles[2]);
        v.x = v.x * c; v.x = v.x - fa0; v.y = v.y * c; v.y = v.y + t3;
        return v;
    }
    // mathXZInTri: integer edge signs with 32-bit wrapping products.
    static bool xzInTriangle(int32_t x, int32_t z, const std::array<int16_t, 3> &a, const std::array<int16_t, 3> &b,
                             const std::array<int16_t, 3> &c) {
        auto cross = [](int32_t px, int32_t pz, const std::array<int16_t, 3> &p, const std::array<int16_t, 3> &q) {
            const auto first = int32_t(uint32_t(int64_t(q[2] - p[2]) * int64_t(px - p[0])));
            const auto second = int32_t(uint32_t(int64_t(q[0] - p[0]) * int64_t(pz - p[2])));
            return int32_t(uint32_t(first) - uint32_t(second)) >= 0;
        };
        const bool ab = cross(x, z, a, b), bc = cross(x, z, b, c);
        if (ab != bc) return false;
        return bc == cross(x, z, c, a);
    }
};
}
