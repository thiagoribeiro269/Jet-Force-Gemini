#pragma once
#include "scene_assets.h"
#include <optional>
#include <limits>
#include <algorithm>

namespace jfg_native {
struct GroundTriangle { Vec3 a, b, c; uint32_t flags; };
struct GroundHit { double height; size_t triangle; };
struct NativeRegion {
    uint32_t level = 0, geometry = 0, blocks = 0, playerObject = 0;
    std::string name;
    Vec3 sourceSpawn{}, boundsMin{}, boundsMax{};
    float yaw = 0, playerScale = 1;
    std::shared_ptr<const AssetPackage> mesh;
    std::vector<GroundTriangle> triangles;
    // Vertical geometry query only. It is not swept collision or game physics.
    std::optional<GroundHit> groundBelow(double x, double z, double ceiling) const {
        if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(ceiling)) throw std::runtime_error("Invalid native ground probe");
        std::optional<GroundHit> result;
        for (size_t i = 0; i < triangles.size(); ++i) {
            const auto &t = triangles[i]; if (t.flags & 0x80) continue;
            const double ax = t.a[0], az = t.a[2], bx = t.b[0], bz = t.b[2], cx = t.c[0], cz = t.c[2];
            const double denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (std::abs(denominator) < 1e-8) continue;
            const double a = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / denominator;
            const double b = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / denominator;
            const double c = 1 - a - b;
            if (a < -1e-8 || b < -1e-8 || c < -1e-8) continue;
            const double height = a * t.a[1] + b * t.b[1] + c * t.c[1];
            if (height <= ceiling + 1e-6 && (!result || height > result->height)) result = GroundHit{height, i};
        }
        return result;
    }
};
inline std::shared_ptr<const NativeRegion> loadRegion(const char *meshPath, const char *infoPath) {
    auto result = std::make_shared<NativeRegion>();
    result->mesh = std::make_shared<const AssetPackage>(loadAssetPackage(meshPath));
    AssetReader reader(infoPath);
    assetRequire(std::memcmp(reader.take(8), "JFGREG1\0", 8) == 0, "Wrong native region metadata");
    result->level = reader.u32(); result->geometry = reader.u32(); result->blocks = reader.u32();
    const auto visible = reader.u32(), triangleCount = reader.u32(); result->playerObject = reader.u32();
    assetRequire(result->level == 21 && result->geometry == 17 && result->blocks == 13 && visible == 2018 && triangleCount == 2342 &&
                 result->playerObject == 126, "Unsupported native region profile");
    result->sourceSpawn = reader.vec3(); result->yaw = reader.f32(); result->playerScale = reader.f32();
    result->boundsMin = reader.vec3(); result->boundsMax = reader.vec3();
    const auto *name = reinterpret_cast<const char *>(reader.take(32));
    const auto *terminator = static_cast<const char *>(std::memchr(name, 0, 32));
    assetRequire(terminator, "Unterminated region name"); result->name.assign(name, terminator);
    assetRequire(result->name == "Forest First" && result->sourceSpawn == Vec3{40, 19, 841} &&
                 std::abs(result->playerScale - 0.26f) < 1e-7f && result->yaw == 0, "Region spawn binding differs");
    assetRequire(result->mesh->worldGeometry && !result->mesh->rigged && result->mesh->vertices.size() == visible * 3,
                 "Region mesh/metadata mismatch");
    for (const auto &draw : result->mesh->draws) assetRequire(draw.model < 0x10000 + result->blocks, "World draw outside source blocks");
    for (unsigned axis = 0; axis < 3; ++axis) assetRequire(result->boundsMin[axis] < result->boundsMax[axis], "Invalid region bounds");
    for (uint32_t i = 0; i < triangleCount; ++i) {
        GroundTriangle triangle{reader.vec3(), reader.vec3(), reader.vec3(), reader.u32()};
        assetRequire(triangle.flags == 0 || triangle.flags == 0x40, "Unknown collision face flags");
        for (const auto &point : {triangle.a, triangle.b, triangle.c}) for (unsigned axis = 0; axis < 3; ++axis)
            assetRequire(point[axis] >= result->boundsMin[axis] && point[axis] <= result->boundsMax[axis], "Ground vertex outside region");
        result->triangles.push_back(triangle);
    }
    assetRequire(reader.offset == reader.bytes.size(), "Trailing region metadata payload");
    const auto floor = result->groundBelow(40, 841, 19);
    assetRequire(floor && std::abs(floor->height + 2) < 1e-8, "Source entry point no longer has its inspected floor");
    return result;
}
inline float poseMinimumY(const AssetPackage &assets, const std::vector<Matrix> &bones) {
    assetRequire(assets.rigged && !assets.vertices.empty() && bones.size() == assets.skeleton.size(), "Invalid ground-alignment pose");
    float result = std::numeric_limits<float>::max();
    for (size_t i = 0; i < assets.vertices.size(); ++i) {
        const auto &v = assets.vertices[i]; result = std::min(result, transform({v.x, v.y, v.z}, bones[assets.vertexBones[i]])[1]);
    }
    return result;
}
}
