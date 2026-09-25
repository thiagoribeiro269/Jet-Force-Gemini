#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include <utility>

namespace RT64 { struct State; }

namespace jfg {

// The input is RT64's word-swapped RDRAM, not a big-endian ROM byte stream.
// This is an intentionally bounded F3DJFG subset for the model-35 proof.
struct Corner {
    uint8_t vertex = 0;
    int16_t s = 0;
    int16_t t = 0;
};

struct Triangle {
    std::array<Corner, 3> corners{};
};

struct Command {
    uint32_t w0 = 0;
    uint32_t w1 = 0;
};

struct DecodedModel {
    uint32_t vertexAddress = 0;
    uint32_t polygonAddress = 0;
    uint32_t textureDmaAddress = 0;
    uint32_t imageAddress = 0;
    uint32_t vertexCount = 0;
    uint32_t geometryMode = 0;
    std::vector<Triangle> triangles;
    std::vector<Command> rdpCommands;
    uint32_t commandCount = 0;
};

// Pure inspection entry point, useful for CPU tests. Every address and opcode
// is checked; unsupported matrix, billboard and lighting commands fail closed.
DecodedModel decode_model(const uint8_t *rdram, uint32_t rdramBytes,
                          uint32_t listAddress, uint32_t vertexBase);

struct DrawStats {
    uint32_t commands = 0;
    uint32_t vertices = 0;
    uint32_t triangles = 0;
    uint32_t rdpCommands = 0;
};

// Caller configures the controlled matrix, viewport, framebuffer and RT64
// workload first. scratchBase reserves at least 96 aligned RDRAM bytes.
// This does not execute the original RSP microcode or submit/fullSync a frame.
DrawStats draw_model(RT64::State &state, uint32_t listAddress,
                     uint32_t vertexBase, uint32_t scratchBase);

// Separate bounded path for a static character. The CPU model instance owns
// float matrices, not RSP fixed matrices. Only translation-only neutral poses
// are accepted; the controlled camera supplies the common transform.
struct StaticCorner {
    uint32_t address = 0;
    int16_t s = 0, t = 0;
    std::array<float, 3> translation{};
};
struct StaticStep {
    bool triangle = false;
    Command rdp{};
    std::array<StaticCorner, 3> corners{};
    bool twoSided = false;
};
struct StaticModel {
    std::vector<StaticStep> steps;
    std::vector<std::pair<uint32_t, uint32_t>> sources;
    DrawStats stats{};
    uint32_t matrixLoads = 0, textureLoads = 0;
};
StaticModel decode_static_character(const uint8_t *rdram, uint32_t rdramBytes,
    uint32_t listAddress, uint32_t vertexBase, uint32_t floatMatrixBase);
DrawStats draw_static_character(RT64::State &state, const StaticModel &model,
    uint32_t scratchBase);

} // namespace jfg
