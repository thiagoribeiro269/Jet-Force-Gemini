#pragma once
#include "animation.h"
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

namespace jfg_native {
inline void assetRequire(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct AssetReader {
    std::vector<uint8_t> bytes;
    size_t offset = 0;
    explicit AssetReader(const char *path) {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        assetRequire(bool(in), "Cannot open private scene");
        const auto size = in.tellg();
        assetRequire(size > 24 && size < 64 * 1024 * 1024, "Scene size outside bounds");
        bytes.resize(size_t(size)); in.seekg(0); in.read(reinterpret_cast<char *>(bytes.data()), size);
        assetRequire(bool(in), "Scene read failed");
    }
    const uint8_t *take(size_t length) {
        assetRequire(offset <= bytes.size() && length <= bytes.size() - offset, "Truncated scene");
        const auto *p = bytes.data() + offset; offset += length; return p;
    }
    uint32_t u32() { uint32_t v; std::memcpy(&v, take(4), 4); return v; }
    float f32() { float v; std::memcpy(&v, take(4), 4); assetRequire(std::isfinite(v) && std::abs(v) < 8192, "Invalid scene float"); return v; }
    jfg_native::Vec3 vec3() { return {f32(), f32(), f32()}; }
};
struct TextureData { uint32_t id, width, height; std::vector<uint8_t> pixels; };
struct MeshDraw { uint32_t first, count, texture, flags, model; };
struct MeshVertex { float x, y, z, u, v, r, g, b, a; };
static_assert(sizeof(MeshVertex) == 36);

struct AssetPackage {
    bool rigged, multipleClips;
    std::vector<TextureData> textures;
    std::vector<MeshDraw> draws;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> vertexBones;
    std::vector<Bone> skeleton;
    std::vector<Clip> clips;
    bool worldGeometry = false;
};
inline AssetPackage loadAssetPackage(const char *input) {
    AssetReader reader(input);
    const auto *magic = reader.take(8);
    const bool multipleClips = std::memcmp(magic, "JFGNAT3\0", 8) == 0;
    const bool rigged = multipleClips || std::memcmp(magic, "JFGNAT2\0", 8) == 0;
    const bool worldGeometry = std::memcmp(magic, "JFGWRL1\0", 8) == 0;
    assetRequire(worldGeometry || rigged || std::memcmp(magic, "JFGNAT1\0", 8) == 0, "Wrong native scene magic");
    const uint32_t textureCount = reader.u32(), drawCount = reader.u32(), vertexCount = reader.u32();
    const uint32_t boneCount = reader.u32();
    assetRequire(boneCount == (rigged ? 21U : 0U) && textureCount && textureCount <= (worldGeometry ? 256U : 64U) &&
            drawCount && drawCount <= (worldGeometry ? 8192U : 1024U) && vertexCount &&
            vertexCount <= (worldGeometry ? 1000000U : 65536U) && vertexCount % 3 == 0, "Unsupported scene counts");
    std::vector<TextureData> textures;
    for (uint32_t t = 0; t < textureCount; ++t) {
        TextureData texture;
        texture.id = reader.u32(); texture.width = reader.u32(); texture.height = reader.u32();
        const auto length = reader.u32();
        assetRequire(texture.width && texture.width <= 1024 && texture.height && texture.height <= 1024 &&
                uint64_t(texture.width) * texture.height * 4 == length, "Invalid RGBA8 texture");
        const auto *pixels = reader.take(length); texture.pixels.assign(pixels, pixels + length);
        textures.push_back(std::move(texture));
    }
    std::vector<MeshDraw> draws;
    uint32_t coveredVertices = 0;
    for (uint32_t d = 0; d < drawCount; ++d) {
        MeshDraw draw{reader.u32(), reader.u32(), reader.u32(), reader.u32(), reader.u32()};
        assetRequire(draw.first == coveredVertices && draw.first <= vertexCount && draw.count && draw.count % 3 == 0 &&
                draw.count <= vertexCount - draw.first && draw.texture < textureCount && draw.flags < (worldGeometry ? 16U : 4U) &&
                (worldGeometry ? (draw.model >= 0x10000 && draw.model < 0x20000) : (draw.model == 220 || draw.model == 309)), "Invalid native draw range");
        coveredVertices += draw.count; draws.push_back(draw);
    }
    assetRequire(coveredVertices == vertexCount, "Mesh coverage incomplete");
    std::vector<MeshVertex> vertices(vertexCount);
    std::vector<uint32_t> vertexBones(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        std::memcpy(&vertices[i], reader.take(sizeof(MeshVertex)), sizeof(MeshVertex));
        if (rigged) { vertexBones[i] = reader.u32(); assetRequire(vertexBones[i] < boneCount, "Invalid vertex bone"); }
    }
    std::vector<jfg_native::Bone> skeleton;
    std::vector<jfg_native::Clip> clips;
    if (rigged) {
        for (uint32_t i = 0; i < boneCount; ++i) {
            const auto parent = int32_t(reader.u32());
            assetRequire(parent >= -1 && parent < int32_t(i) && (parent == -1) == (i == 0), "Invalid bone hierarchy");
            skeleton.push_back({parent, reader.vec3()});
        }
        const uint32_t clipCount = multipleClips ? reader.u32() : 1;
        assetRequire(multipleClips ? (clipCount == 2 || clipCount == 3 || clipCount == 9 || clipCount == 19) : clipCount == 1,
                     "Unsupported clip collection size");
        for (uint32_t i = 0; i < clipCount; ++i) {
            jfg_native::Clip clip;
            clip.id = reader.u32(); const uint32_t keyCount = reader.u32(), looping = reader.u32();
            clip.sourceRate = reader.f32();
            const uint32_t ids[] = {1026, 1030, 1071, 1027, 1028, 1025, 1019, 1055, 1061,
                                    1039, 1040, 1041, 1042, 1043, 1044, 1020, 1021, 1048, 1050};
            const uint32_t counts[] = {16, 10, 3, 16, 16, 16, 50, 16, 16, 31, 21, 21, 16, 16, 6, 40, 50, 9, 11};
            const uint32_t loops[] = {1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0};
            assetRequire(clip.id == ids[i] && keyCount == counts[i] && looping == loops[i] && clip.sourceRate == 15,
                    "Unsupported animation profile");
            clip.loop = looping != 0;
            for (uint32_t k = 0; k < keyCount; ++k) {
                jfg_native::Keyframe key; key.root = reader.vec3();
                for (uint32_t b = 0; b < boneCount; ++b) key.angles.push_back(reader.vec3());
                clip.keys.push_back(std::move(key));
            }
            clips.push_back(std::move(clip));
        }
    }
    assetRequire(reader.offset == reader.bytes.size(), "Trailing scene payload");
    for (const auto &vertex : vertices) {
        std::array<float, 9> values{}; std::memcpy(values.data(), &vertex, sizeof(vertex));
        for (float value : values) assetRequire(std::isfinite(value) && std::abs(value) < (worldGeometry ? 65536 : 8192), "Invalid vertex value");
    }
    return {rigged, multipleClips, std::move(textures), std::move(draws), std::move(vertices),
            std::move(vertexBones), std::move(skeleton), std::move(clips), worldGeometry};
}
}
