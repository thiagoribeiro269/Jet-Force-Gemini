#include "adapter.h"

#include <cstring>
#include <cmath>
#include <stdexcept>

#ifndef JFG_ADAPTER_DECODE_ONLY
#include "hle/rt64_rdp.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_state.h"
#include "gbi/rt64_gbi_rdp.h"
#endif

namespace jfg {
namespace {

constexpr uint32_t kMaxCommands = 11;
constexpr uint32_t kMaxDmaCommands = 8;
constexpr uint32_t kMaxTriangles = 8;
#ifndef JFG_ADAPTER_DECODE_ONLY
constexpr uint32_t kScratchBytes = 6 * 16;
#endif

uint32_t physical(uint32_t address) {
    // KSEG0/KSEG1 and RSP DMA physical addresses are sufficient for this proof.
    if ((address & 0xE0000000U) == 0x80000000U ||
        (address & 0xE0000000U) == 0xA0000000U) {
        return address & 0x1FFFFFFFU;
    }
    if ((address & 0xFF000000U) == 0) {
        return address;
    }
    throw std::runtime_error("F3DJFG: unsupported address space");
}

void check_range(uint32_t address, uint32_t bytes, uint32_t size) {
    const uint32_t start = physical(address);
    if (bytes > size || start > size - bytes) {
        throw std::runtime_error("F3DJFG: RDRAM range out of bounds");
    }
}

uint8_t be8(const uint8_t *ram, uint32_t address, uint32_t size) {
    check_range(address, 1, size);
    return ram[physical(address) ^ 3U];
}

uint16_t be16(const uint8_t *ram, uint32_t address, uint32_t size) {
    return (uint16_t(be8(ram, address, size)) << 8) |
           be8(ram, address + 1, size);
}

uint32_t be32(const uint8_t *ram, uint32_t address, uint32_t size) {
    check_range(address, 4, size);
    uint32_t value;
    std::memcpy(&value, ram + physical(address), sizeof(value));
    return value;
}

Command command_at(const uint8_t *ram, uint32_t address, uint32_t size) {
    if ((physical(address) & 7U) != 0) {
        throw std::runtime_error("F3DJFG: unaligned display list");
    }
    check_range(address, 8, size);
    return {be32(ram, address, size), be32(ram, address + 4, size)};
}

#ifndef JFG_ADAPTER_DECODE_ONLY
void put8(uint8_t *ram, uint32_t address, uint8_t value) {
    ram[address ^ 3U] = value;
}

void put16(uint8_t *ram, uint32_t address, uint16_t value) {
    put8(ram, address, uint8_t(value >> 8));
    put8(ram, address + 1, uint8_t(value));
}

void rdp_command(RT64::State &state, const Command &command) {
    RT64::DisplayList dl;
    dl.w0 = command.w0;
    dl.w1 = command.w1;
    RT64::DisplayList *at = &dl;
    switch (command.w0 >> 24) {
    case 0xE6: RT64::GBI_RDP::loadSync(&state, &at); break;
    case 0xE7: RT64::GBI_RDP::pipeSync(&state, &at); break;
    case 0xEF: RT64::GBI_RDP::setOtherMode(&state, &at); break;
    case 0xF2: RT64::GBI_RDP::setTileSize(&state, &at); break;
    case 0xF3: RT64::GBI_RDP::loadBlock(&state, &at); break;
    case 0xF5: RT64::GBI_RDP::setTile(&state, &at); break;
    case 0xFC: RT64::GBI_RDP::setCombine(&state, &at); break;
    case 0xFD: RT64::GBI_RDP::setTextureImage(&state, &at); break;
    default: throw std::runtime_error("F3DJFG: unsupported RDP command");
    }
}
#endif

void validate_rdp_command(const uint8_t *ram, uint32_t size,
                          const Command &command) {
    switch (command.w0 >> 24) {
    case 0xE6: case 0xE7:
        if ((command.w0 & 0x00FFFFFFU) != 0 || command.w1 != 0) {
            throw std::runtime_error("F3DJFG: unsupported RDP sync payload");
        }
        break;
    case 0xEF:
        if (command.w0 != 0xEF182C0FU || command.w1 != 0xC81049D8U) {
            throw std::runtime_error("F3DJFG: unsupported other mode");
        }
        break;
    case 0xF2:
        if (command.w0 != 0xF2000000U || command.w1 != 0x0007C0FCU) {
            throw std::runtime_error("F3DJFG: unsupported tile size");
        }
        break;
    case 0xF3:
        if (command.w0 != 0xF3000000U || command.w1 != 0x077FF000U) {
            throw std::runtime_error("F3DJFG: unsupported texture load block");
        }
        break;
    case 0xF5:
        if (!((command.w0 == 0xF5100000U && command.w1 == 0x07080200U) ||
              (command.w0 == 0xF5101000U && command.w1 == 0x00080200U))) {
            throw std::runtime_error("F3DJFG: unsupported tile setup");
        }
        break;
    case 0xFC:
        if (command.w0 != 0xFC121603U || command.w1 != 0xFFFFFFF8U) {
            throw std::runtime_error("F3DJFG: unsupported combine mode");
        }
        break;
    case 0xFD:
        // The source image is RGBA16, 32 x 64, loaded by the observed block.
        if (command.w0 != 0xFD100000U) {
            throw std::runtime_error("F3DJFG: unsupported texture image format");
        }
        check_range(command.w1, 4096, size);
        break;
    default:
        throw std::runtime_error("F3DJFG: unsupported RDP command");
    }
    (void)ram;
}

} // namespace

DecodedModel decode_model(const uint8_t *ram, uint32_t size,
                          uint32_t listAddress, uint32_t vertexBase) {
    if (ram == nullptr || size == 0 || size > 0x800000U) {
        throw std::runtime_error("F3DJFG: invalid RDRAM view");
    }
    if ((size & 3U) != 0) {
        throw std::runtime_error("F3DJFG: RDRAM size is not word aligned");
    }
    if ((physical(vertexBase) & 7U) != 0) {
        throw std::runtime_error("F3DJFG: unaligned vertex DMA base");
    }
    DecodedModel result;
    bool sawVertex = false, sawPolygon = false, sawTextureDma = false, ended = false;
    constexpr uint8_t expectedMain[kMaxCommands] = {
        0xE7, 0xB7, 0xFD, 0x07, 0xE7, 0xFC, 0xEF, 0x04, 0x05, 0xE7, 0xB8
    };
    for (uint32_t i = 0; i < kMaxCommands; ++i) {
        const Command cmd = command_at(ram, listAddress + i * 8, size);
        if ((cmd.w0 >> 24) != expectedMain[i]) {
            throw std::runtime_error("F3DJFG: unsupported model command order");
        }
        result.commandCount++;
        switch (cmd.w0 >> 24) {
        case 0x04: {
            if (sawVertex || cmd.w0 != 0x04200030U || cmd.w1 != 0) {
                throw std::runtime_error("F3DJFG: unsupported vertex DMA encoding");
            }
            // 0x30 includes an eight-byte prefix before four ten-byte records.
            check_range(vertexBase, 40, size);
            result.vertexAddress = vertexBase;
            result.vertexCount = 4;
            sawVertex = true;
            break;
        }
        case 0x05: {
            if (!sawVertex || sawPolygon || cmd.w0 != 0x05110020U) {
                throw std::runtime_error("F3DJFG: unsupported polygon encoding");
            }
            check_range(cmd.w1, 32, size);
            result.polygonAddress = cmd.w1;
            for (uint32_t tri = 0; tri < 2; ++tri) {
                const uint32_t p = cmd.w1 + tri * 16;
                if (be8(ram, p, size) != 0) {
                    throw std::runtime_error("F3DJFG: unsupported polygon flags");
                }
                Triangle decoded;
                for (uint32_t corner = 0; corner < 3; ++corner) {
                    const uint8_t index = be8(ram, p + 1 + corner, size);
                    if (index >= result.vertexCount) {
                        throw std::runtime_error("F3DJFG: vertex index out of range");
                    }
                    decoded.corners[corner] = {
                        index,
                        int16_t(be16(ram, p + 4 + corner * 4, size)),
                        int16_t(be16(ram, p + 6 + corner * 4, size))
                    };
                }
                result.triangles.push_back(decoded);
            }
            if (result.triangles.size() > kMaxTriangles) {
                throw std::runtime_error("F3DJFG: polygon budget exceeded");
            }
            sawPolygon = true;
            break;
        }
        case 0x07: {
            if (sawTextureDma || cmd.w0 != 0x07060030U) {
                throw std::runtime_error("F3DJFG: unsupported texture DMA encoding");
            }
            if (result.imageAddress == 0) {
                throw std::runtime_error("F3DJFG: texture DMA precedes image source");
            }
            constexpr uint32_t count = 6;
            static_assert(count <= kMaxDmaCommands);
            check_range(cmd.w1, count * 8, size);
            result.textureDmaAddress = cmd.w1;
            constexpr uint8_t expected[] = {0xF5, 0xE6, 0xF3, 0xE7, 0xF5, 0xF2};
            for (uint32_t j = 0; j < count; ++j) {
                const Command nested = command_at(ram, cmd.w1 + j * 8, size);
                if ((nested.w0 >> 24) != expected[j]) {
                    throw std::runtime_error("F3DJFG: unexpected texture DMA command");
                }
                if (j == 0 && (nested.w0 != 0xF5100000U ||
                               nested.w1 != 0x07080200U)) {
                    throw std::runtime_error("F3DJFG: unsupported load tile");
                }
                if (j == 4 && (nested.w0 != 0xF5101000U ||
                               nested.w1 != 0x00080200U)) {
                    throw std::runtime_error("F3DJFG: unsupported render tile");
                }
                validate_rdp_command(ram, size, nested);
                result.rdpCommands.push_back(nested);
            }
            sawTextureDma = true;
            break;
        }
        case 0xB7:
            if (cmd.w0 != 0xB7000000U || cmd.w1 != 0x00010205U) {
                throw std::runtime_error("F3DJFG: unsupported geometry mode");
            }
            result.geometryMode = cmd.w1;
            break;
        case 0xB8:
            if (cmd.w0 != 0xB8000000U || cmd.w1 != 0 ||
                !sawVertex || !sawPolygon || !sawTextureDma ||
                result.geometryMode != 0x00010205U || result.imageAddress == 0) {
                throw std::runtime_error("F3DJFG: incomplete model list");
            }
            ended = true;
            break;
        default:
            validate_rdp_command(ram, size, cmd);
            if ((cmd.w0 >> 24) == 0xFD) {
                if (result.imageAddress != 0) {
                    throw std::runtime_error("F3DJFG: multiple texture images");
                }
                result.imageAddress = cmd.w1;
            }
            result.rdpCommands.push_back(cmd);
            break;
        }
        if (ended) break;
    }
    if (!ended) throw std::runtime_error("F3DJFG: command budget exceeded");
    return result;
}

#ifndef JFG_ADAPTER_DECODE_ONLY
DrawStats draw_model(RT64::State &state, uint32_t listAddress,
                     uint32_t vertexBase, uint32_t scratchBase) {
    // State::RDRAMSize names the largest index; the supplied 8 MiB buffer has
    // RDRAMSize+1 bytes. The scratch space is owned by the caller.
    constexpr uint32_t ramBytes = 0x800000U;
    if (!state.RDRAM || !state.rsp || !state.rdp || !state.ext.workloadQueue) {
        throw std::runtime_error("F3DJFG: RT64 state is incomplete");
    }
    check_range(scratchBase, kScratchBytes, ramBytes);
    const uint32_t scratch = physical(scratchBase);
    if ((scratch & 7U) != 0) {
        throw std::runtime_error("F3DJFG: unaligned scratch buffer");
    }
    const DecodedModel decoded = decode_model(state.RDRAM, ramBytes,
                                               listAddress, vertexBase);
    const auto overlaps = [scratch](uint32_t address, uint32_t bytes) {
        const uint32_t p = physical(address);
        return scratch < p + bytes && p < scratch + kScratchBytes;
    };
    if (overlaps(listAddress, decoded.commandCount * 8) ||
        overlaps(decoded.vertexAddress, decoded.vertexCount * 10) ||
        overlaps(decoded.polygonAddress, uint32_t(decoded.triangles.size() * 16)) ||
        overlaps(decoded.textureDmaAddress, 48) ||
        (decoded.imageAddress != 0 && overlaps(decoded.imageAddress, 4096))) {
        throw std::runtime_error("F3DJFG: scratch overlaps source data");
    }

    // Preserve the observed mode. Its fog bit matters even for this controlled
    // frame: the observed RDP blender uses shade alpha as the fog factor.
    // The caller supplies controlled fog values; no original camera/fog state
    // is inferred from this isolated list.
    state.rsp->setGeometryMode(0x00010205U);
    state.rsp->setTexture(0, 0, 1, 0xFFFFU, 0xFFFFU);

    // Preserve command order: FD before the six-command DMA list; geometry
    // commands are emitted only after all preceding RDP state is forwarded.
    uint32_t rdpCursor = 0;
    uint32_t triCursor = 0;
    for (uint32_t i = 0; i < decoded.commandCount; ++i) {
        const Command cmd = command_at(state.RDRAM, listAddress + i * 8,
                                        ramBytes);
        switch (cmd.w0 >> 24) {
        case 0x04: case 0xB7: case 0xB8:
            break;
        case 0x07:
            for (uint32_t j = 0; j < 6; ++j) {
                rdp_command(state, decoded.rdpCommands.at(rdpCursor++));
            }
            break;
        case 0x05: {
            // Each corner gets its own canonical 16-byte N64 vertex. This is
            // required because the same position can have different UVs.
            for (const Triangle &tri : decoded.triangles) {
                for (uint32_t corner = 0; corner < 3; ++corner) {
                    const Corner &c = tri.corners[corner];
                    const uint32_t src = decoded.vertexAddress + c.vertex * 10;
                    const uint32_t dst = scratch + corner * 16;
                    for (uint32_t b = 0; b < 6; ++b) {
                        put8(state.RDRAM, dst + b,
                             be8(state.RDRAM, src + b, ramBytes));
                    }
                    put16(state.RDRAM, dst + 6, 0);
                    put16(state.RDRAM, dst + 8, uint16_t(c.s));
                    put16(state.RDRAM, dst + 10, uint16_t(c.t));
                    for (uint32_t b = 0; b < 4; ++b) {
                        put8(state.RDRAM, dst + 12 + b,
                             be8(state.RDRAM, src + 6 + b, ramBytes));
                    }
                }
                state.rsp->setVertex(scratch, 3, 0);
                state.rsp->drawIndexedTri(0, 1, 2);
                ++triCursor;
            }
            break;
        }
        default:
            rdp_command(state, decoded.rdpCommands.at(rdpCursor++));
            break;
        }
    }
    if (rdpCursor != decoded.rdpCommands.size() ||
        triCursor != decoded.triangles.size()) {
        throw std::runtime_error("F3DJFG: decoded command accounting mismatch");
    }
    return {decoded.commandCount, uint32_t(triCursor * 3), triCursor,
            uint32_t(rdpCursor)};
}
#endif

StaticModel decode_static_character(const uint8_t *ram, uint32_t size,
    uint32_t listAddress, uint32_t vertexBase, uint32_t floatMatrixBase) {
    const auto need = [](bool ok, const char *why) {
        if (!ok) throw std::runtime_error(why);
    };
    need(ram && size && size <= 0x800000 && !(size & 3), "Character: invalid RAM view");
    vertexBase = physical(vertexBase);
    floatMatrixBase = physical(floatMatrixBase);
    need(!(vertexBase & 7) && !(floatMatrixBase & 7), "Character: unaligned DMA base");
    StaticModel result;
    auto source = [&](uint32_t address, uint32_t bytes) {
        check_range(address, bytes, size);
        result.sources.emplace_back(physical(address), bytes);
    };
    auto emit = [&](Command cmd) {
        StaticStep step;
        step.rdp = cmd;
        result.steps.push_back(step);
        ++result.stats.rdpCommands;
    };
    std::array<uint32_t, 32> vertex{};
    std::array<bool, 32> valid{};
    std::array<bool, 4> matrixReady{};
    std::array<std::array<float, 3>, 4> matrixTranslation{};
    std::array<std::array<float, 3>, 32> vertexTranslation{};
    uint32_t selectedMatrix = 0, imageAddress = 0, imageCode = 0;
    bool geometry = false, combine = false, other = false, textureReady = false;
    for (uint32_t i = 0; i < 1024; ++i) {
        const Command cmd = command_at(ram, listAddress + i * 8, size);
        ++result.stats.commands;
        switch (cmd.w0 >> 24) {
        case 0x01: {
            const uint32_t slot = (cmd.w0 >> 16) & 15;
            need(slot >= 1 && slot <= 3 && cmd.w0 == (0x01800040U | slot << 16) &&
                 !(cmd.w1 & 63) && cmd.w1 < 21 * 64, "Character: unsupported matrix DMA");
            const uint32_t address = floatMatrixBase + cmd.w1;
            source(address, 64);
            // Float32 rest-pose matrices, prepared from the bone hierarchy
            // and checked separately against original MIPS gen_anim_data.
            for (uint32_t k = 0; k < 16; ++k) {
                const uint32_t bits = be32(ram, address + k * 4, size);
                if (k >= 12 && k < 15) {
                    float translation;
                    std::memcpy(&translation, &bits, 4);
                    need(std::isfinite(translation) && std::abs(translation) < 1024,
                         "Character: invalid neutral-pose translation");
                    matrixTranslation[slot][k - 12] = translation;
                } else {
                    need(bits == (k % 5 == 0 ? 0x3f800000U : 0U),
                         "Character: rotated/scaled pose needs a different path");
                }
            }
            matrixReady[slot] = true;
            ++result.matrixLoads;
            break;
        }
        case 0xBC:
            need(cmd.w0 == 0xBC00000AU && (cmd.w1 == 64 || cmd.w1 == 128 || cmd.w1 == 192),
                 "Character: unsupported MOVEWORD/billboard");
            selectedMatrix = cmd.w1 >> 6;
            need(matrixReady[selectedMatrix], "Character: matrix selected before loading");
            break;
        case 0x04: {
            const uint32_t count = (cmd.w0 >> 19) & 31;
            const uint32_t first = (cmd.w0 >> 9) & 31;
            // makeModelGfx stores byte-alignment bits in 17..18. Bit 16
            // (append/billboard) is not part of this observed character path.
            const uint32_t encoding = 0x04000000U | (count << 19) |
                ((cmd.w1 & 6) << 16) | (first << 9) | (count * 10 + 8);
            need(count && first + count <= 32 && cmd.w0 == encoding && !(cmd.w1 & 1) &&
                 cmd.w1 < 660 * 10 && cmd.w1 + count * 10 <= 660 * 10 &&
                 selectedMatrix && matrixReady[selectedMatrix], "Character: invalid vertex DMA");
            if (first == 0) valid.fill(false);
            source(vertexBase + cmd.w1, count * 10);
            for (uint32_t v = 0; v < count; ++v) {
                vertex[first + v] = vertexBase + cmd.w1 + v * 10;
                vertexTranslation[first + v] = matrixTranslation[selectedMatrix];
                valid[first + v] = true;
            }
            result.stats.vertices += count;
            break;
        }
        case 0x05: {
            const uint32_t count = ((cmd.w0 >> 20) & 15) + 1;
            need(cmd.w0 == (0x05010000U | ((count - 1) << 20) | count * 16) &&
                 geometry && combine && other && textureReady, "Character: invalid polygon state");
            source(cmd.w1, count * 16);
            for (uint32_t t = 0; t < count; ++t) {
                const uint32_t address = cmd.w1 + t * 16;
                const uint8_t flags = be8(ram, address, size);
                need(flags == 0 || flags == 0x40, "Character: unsupported face flags");
                StaticStep step;
                step.triangle = true;
                step.twoSided = flags == 0x40;
                for (uint32_t c = 0; c < 3; ++c) {
                    const uint32_t index = be8(ram, address + c + 1, size);
                    need(index < 32 && valid[index], "Character: unloaded vertex index");
                    step.corners[c] = {vertex[index], int16_t(be16(ram, address + 4 + c * 4, size)),
                                      int16_t(be16(ram, address + 6 + c * 4, size)), vertexTranslation[index]};
                }
                result.steps.push_back(step);
                need(++result.stats.triangles <= 520, "Character: polygon budget exceeded");
            }
            valid.fill(false);
            break;
        }
        case 0xFD:
            need(cmd.w0 == 0xFD100000U || cmd.w0 == 0xFD700000U || cmd.w0 == 0xFD180000U,
                 "Character: unsupported image format");
            imageAddress = cmd.w1;
            imageCode = cmd.w0;
            textureReady = false;
            emit(cmd);
            break;
        case 0x07: {
            need(cmd.w0 == 0x07060030U && imageAddress && !textureReady,
                 "Character: unsupported texture command DMA");
            source(cmd.w1, 48);
            std::array<Command, 6> nested;
            constexpr uint8_t codes[] = {0xF5, 0xE6, 0xF3, 0xE7, 0xF5, 0xF2};
            for (uint32_t c = 0; c < 6; ++c) {
                nested[c] = command_at(ram, cmd.w1 + c * 8, size);
                need(nested[c].w0 >> 24 == codes[c], "Character: unsupported texture DMA order");
            }
            const auto &load = nested[0], &block = nested[2], &tile = nested[4], &extent = nested[5];
            need(nested[1].w0 == 0xE6000000U && nested[1].w1 == 0 &&
                 nested[3].w0 == 0xE7000000U && nested[3].w1 == 0, "Character: invalid texture sync");
            const uint32_t pixelCode = imageCode & 0x00F80000U;
            const bool rgba32 = imageCode == 0xFD180000U;
            const bool ia8 = imageCode == 0xFD700000U;
            const uint32_t loadBytes = (((block.w1 >> 12) & 0xFFF) + 1) * (rgba32 ? 4 : 2);
            const uint32_t width = ((extent.w1 >> 12) & 0xFFF) / 4 + 1;
            const uint32_t height = (extent.w1 & 0xFFF) / 4 + 1;
            const uint32_t rowBytes = width * (ia8 ? 1 : 2); // RGBA32 has two TMEM banks.
            const uint32_t renderCode = ia8 ? 0x00680000U : pixelCode;
            need(load.w0 == (0xF5000000U | pixelCode) &&
                 load.w1 == (rgba32 ? 0x07080030U : 0x07080200U) &&
                 block.w0 == 0xF3000000U && (block.w1 & 0xFF000FFFU) == 0x07000000U &&
                 extent.w0 == 0xF2000000U && !(extent.w1 & 0xFF003003U) &&
                 width <= 128 && height <= 128 && loadBytes <= 4096 &&
                 loadBytes == width * height * (rgba32 ? 4 : ia8 ? 1 : 2) &&
                 tile.w0 == (0xF5000000U | renderCode | (((rowBytes + 7) / 8) << 9)) &&
                 tile.w1 == (rgba32 ? 0x00080030U : 0x00080200U), "Character: unsupported texture tile/block layout");
            source(imageAddress, loadBytes);
            for (auto c : nested) emit(c);
            textureReady = true;
            ++result.textureLoads;
            break;
        }
        case 0xB7:
            need(cmd.w0 == 0xB7000000U && cmd.w1 == 0x10205 && !geometry,
                 "Character: unsupported geometry mode");
            geometry = true;
            break;
        case 0xFC:
            need((cmd.w0 == 0xFC121603U && cmd.w1 == 0xFFFFFFF8U) ||
                 (cmd.w0 == 0xFC1217FFU && cmd.w1 == 0xFFFFFE38U), "Character: unsupported combiner");
            combine = true;
            emit(cmd);
            break;
        case 0xEF:
            need(cmd.w0 == 0xEF182C0FU && (cmd.w1 == 0xC81049D8U || cmd.w1 == 0xC8104DD8U ||
                 cmd.w1 == 0xC8112078U), "Character: unsupported othermode");
            other = true;
            emit(cmd);
            break;
        case 0xE7:
            need(cmd.w0 == 0xE7000000U && cmd.w1 == 0, "Character: invalid pipe sync");
            emit(cmd);
            break;
        case 0xB8:
            need(cmd.w0 == 0xB8000000U && cmd.w1 == 0 && result.stats.triangles &&
                 result.matrixLoads && result.textureLoads, "Character: incomplete display list");
            source(listAddress, result.stats.commands * 8);
            return result;
        default: throw std::runtime_error("Character: unsupported opcode");
        }
    }
    throw std::runtime_error("Character: display list command budget exceeded");
}

#ifndef JFG_ADAPTER_DECODE_ONLY
DrawStats draw_static_character(RT64::State &state, const StaticModel &model, uint32_t scratchBase) {
    constexpr uint32_t bytes = 0x800000;
    const uint32_t scratch = physical(scratchBase);
    check_range(scratch, 96, bytes);
    if ((scratch & 7) || !state.RDRAM || !state.rsp || !state.rdp) {
        throw std::runtime_error("Character: invalid renderer/scratch");
    }
    for (const auto &[address, length] : model.sources) {
        if (scratch < address + length && address < scratch + 96) {
            throw std::runtime_error("Character: scratch overlaps source");
        }
    }
    state.rsp->setTexture(0, 0, 1, 0xFFFFU, 0xFFFFU);
    for (const auto &step : model.steps) {
        if (!step.triangle) { rdp_command(state, step.rdp); continue; }
        // Positive viewport X, F3D cull-back=0x2000; bit 0x40 is two-sided.
        const uint32_t desiredCull = step.twoSided ? 0U : 0x2000U;
        // Set face culling explicitly. This diagnostic does not yet optimize
        // the resulting RT64 draw-call count or transformation reuse.
        state.rsp->clearGeometryMode(0x3000U & ~desiredCull);
        state.rsp->setGeometryMode(0x10205U | desiredCull);
        for (uint32_t c = 0; c < 3; ++c) {
            const auto &corner = step.corners[c];
            const uint32_t dst = scratch + c * 16;
            for (uint32_t b = 0; b < 6; ++b) put8(state.RDRAM, dst + b, be8(state.RDRAM, corner.address + b, bytes));
            put16(state.RDRAM, dst + 6, 0);
            put16(state.RDRAM, dst + 8, uint16_t(corner.s));
            put16(state.RDRAM, dst + 10, uint16_t(corner.t));
            for (uint32_t b = 0; b < 4; ++b) put8(state.RDRAM, dst + 12 + b, be8(state.RDRAM, corner.address + 6 + b, bytes));
            auto transform = hlslpp::float4x4::identity();
            for (uint32_t axis = 0; axis < 3; ++axis) transform[3][axis] = corner.translation[axis];
            state.rsp->modelMatrixStack[0] = transform;
            state.rsp->modelViewProjChanged = true;
            state.rsp->setVertex(dst, 1, c);
        }
        state.rsp->drawIndexedTri(0, 1, 2);
    }
    return model.stats;
}
#endif

} // namespace jfg
