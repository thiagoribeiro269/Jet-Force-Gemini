#pragma once
#include "original_math.h"
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace jfg_native {

// Native port of the original track collision engine for converted regions.
// Load-time derivations follow overlay 24 (0x01800908 masks, 0x01800CE4
// planes and exposed edges); queries follow track.c/trackasm.s:
// trackMakePolylist, getXZCompareMask, getYCompareMask, func_800182C0 (edge
// list), func_80016EA0 (plane test), func_800175A0 (edge test),
// trackCylinderIntersect, trackSphereIntersect, trackGetPlayerIntersect and
// trackPolyHeight. Object hit models (hitPoly*) are not integrated: the
// region scenes instantiate no objects, so those lists are empty.
struct TrackBox { int16_t minX, minY, minZ, maxX, maxY, maxZ; };
struct TrackFace { uint8_t flags; std::array<uint8_t, 3> vertex; };
struct TrackBatch { uint8_t texture; uint16_t firstVertex, firstTriangle; uint32_t flags; };
struct TrackPlane { float nx = 0, ny = 0, nz = 0, d = 0; };
struct TrackBlock {
    TrackBox box{};
    std::vector<std::array<int16_t, 3>> vertices;
    std::vector<TrackFace> faces;
    std::vector<TrackBatch> batches;                 // batch count + sentinel
    std::vector<std::array<uint16_t, 4>> links;      // face plane, three edge-plane references
    std::vector<uint32_t> xzMask;                    // +0x10: X cells in bits 0..15, Z cells in 16..31
    std::vector<uint8_t> yMask, edgeMask;            // +0x14 and +0x20
    std::vector<TrackPlane> planes;                  // +0x1C
    size_t batchCount() const { return batches.size() - 1; }
    const std::array<int16_t, 3> &corner(const TrackBatch &batch, const TrackFace &face, unsigned k) const {
        return vertices.at(size_t(batch.firstVertex) + face.vertex[k]);
    }
};
// Original per-sphere result, 0x28 bytes, named by offset.
struct TrackHit {
    int32_t object = -1;          // +0x00, object hit models are not integrated
    Vec3f normal;                 // +0x04
    Vec3f push;                   // +0x10
    float distance = 1.024e9f;    // +0x1C
    uint32_t surfaceFlags = 0;    // +0x20 batch flags
    uint8_t surface = 0;          // +0x24 texture table byte +7
    uint8_t type = 0;             // +0x25: 2 floor, 4 wall, 8 ceiling, 0x80 stuck, 1 selected
    uint8_t edgeSpecial = 0;      // +0x26
    uint8_t objectKind = 0;       // +0x27, written by object tests only
};
struct TrackEdge { Vec3f p0, p1, dir; float length = 0; uint32_t block = 0; uint16_t batch = 0; uint16_t special = 0; };
struct PolyEntry { bool marker; uint32_t block; uint16_t triangle, batch; };

class TrackCollision {
public:
    uint32_t level = 0, geometry = 0, polyCapacity = 0, edgeCapacity = 0;
    bool noHits = false;
    std::array<int16_t, 6> extents{};  // geometry header +0x20: minX, maxX, minY, maxY, minZ, maxZ
    std::vector<uint8_t> surfaces;
    std::vector<TrackBlock> blocks;
    OriginalMath math;

    // 0x01800908: per-triangle occupancy of 16 X, 16 Z and 8 Y cells.
    static void computeMasks(TrackBlock &block) {
        const auto &box = block.box;
        block.xzMask.assign(block.faces.size(), 0);
        block.yMask.assign(block.faces.size(), 0);
        for (size_t b = 0; b < block.batchCount(); ++b) {
            const auto &batch = block.batches[b];
            for (size_t t = batch.firstTriangle; t < block.batches[b + 1].firstTriangle; ++t) {
                const auto &face = block.faces[t];
                if (face.flags & 0x80) { block.xzMask[t] = 0; block.yMask[t] = 0; continue; }
                int32_t maxX = -0x7FBC, minX = 0x7FBC, maxY = -0x7FBC, minY = 0x7FBC, maxZ = -0x7FBC, minZ = 0x7FBC;
                for (unsigned k = 0; k < 3; ++k) {
                    const auto &v = block.corner(batch, face, k);
                    if (maxX < v[0]) maxX = v[0];
                    if (v[0] < minX) minX = v[0];
                    if (maxY < v[1]) maxY = v[1];
                    if (v[1] < minY) minY = v[1];
                    if (maxZ < v[2]) maxZ = v[2];
                    if (v[2] < minZ) minZ = v[2];
                }
                uint32_t mask = 0, bit = 1;
                int32_t cell = ((box.maxX - box.minX) >> 4) + 1, lower = box.minX, upper = cell + box.minX;
                for (int i = 0; i < 16; ++i, bit <<= 1, upper += cell, lower += cell)
                    if (upper >= minX && maxX >= lower) mask |= bit;
                cell = ((box.maxZ - box.minZ) >> 4) + 1; lower = box.minZ; upper = cell + box.minZ;
                for (int i = 0; i < 16; ++i, bit <<= 1, upper += cell, lower += cell)
                    if (upper >= minZ && maxZ >= lower) mask |= bit;
                block.xzMask[t] = mask;
                uint32_t yMask = 0, yBit = 1;
                cell = ((box.maxY - box.minY) >> 3) + 1; lower = box.minY; upper = cell + box.minY;
                for (int i = 0; i < 8; ++i, yBit <<= 1, upper += cell, lower += cell)
                    if (upper >= minY && maxY >= lower) yMask |= yBit;
                block.yMask[t] = uint8_t(yMask);
            }
        }
    }
    // 0x01800CE4: face planes, shared edge planes (sign flip for the
    // neighbor), exposed-edge mask and ledge flags. Returns the plane count.
    static size_t computePlanes(TrackBlock &block, bool noHits) {
        block.edgeMask.assign(block.faces.size(), 0);
        block.planes.clear();
        for (size_t b = 0; b < block.batchCount(); ++b) {
            const auto &batch = block.batches[b];
            for (size_t t = batch.firstTriangle; t < block.batches[b + 1].firstTriangle; ++t) {
                const auto &face = block.faces[t];
                if (face.flags & 0x80) continue;
                const auto &p0 = block.corner(batch, face, 0), &p1 = block.corner(batch, face, 1), &p2 = block.corner(batch, face, 2);
                const float x0 = p0[0], y0 = p0[1], z0 = p0[2], x1 = p1[0], y1 = p1[1], z1 = p1[2];
                const float z2z1 = float(p2[2]) - z1, y1y0 = y1 - y0, y2y1 = float(p2[1]) - y1, z1z0 = z1 - z0;
                const float x2x1 = float(p2[0]) - x1, x1x0 = x1 - x0;
                float nx = (y1y0 * z2z1) - (z1z0 * y2y1), ny = (z1z0 * x2x1) - (x1x0 * z2z1), nz = (x1x0 * y2y1) - (y1y0 * x2x1);
                const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
                if (length > 0.0f) { nx = nx / length; ny = ny / length; nz = nz / length; }
                block.edgeMask[t] = 0;
                block.planes.push_back({nx, ny, nz, -((x0 * nx) + (y0 * ny) + (z0 * nz))});
            }
        }
        if (noHits) return block.planes.size();
        const size_t facePlanes = block.planes.size();
        for (size_t b = 0; b < block.batchCount(); ++b) {
            const auto &batch = block.batches[b];
            if (batch.flags & 0x880) continue;
            for (size_t t = batch.firstTriangle; t < block.batches[b + 1].firstTriangle; ++t) {
                if (block.faces[t].flags & 0x80) continue;
                const uint16_t self = block.links[t][0];
                for (unsigned k = 0; k < 3; ++k) {
                    const unsigned next = k + 1 >= 3 ? 0 : k + 1, opposite = next + 1 >= 3 ? 0 : next + 1;
                    const uint16_t neighbor = block.links[t][1 + k];
                    if (neighbor >= facePlanes) continue;
                    if (block.faces[t].flags & 3) throw std::runtime_error("Ledge pairing faces are not ported");
                    const TrackPlane own = block.planes.at(self), other = block.planes.at(neighbor);
                    const auto &a = block.corner(batch, block.faces[t], k), &c = block.corner(batch, block.faces[t], next);
                    const auto &o = block.corner(batch, block.faces[t], opposite);
                    const float ax = a[0], ay = a[1], az = a[2], cy = c[1];
                    const float sumZ = other.nz + own.nz;
                    const float up = ((other.ny + own.ny) * 5.0f) + ay;
                    const float ey = cy - ay;
                    const float uz = ((sumZ * 5.0f) + az) - az;
                    const float ez = float(c[2]) - az;
                    const float ux = (((other.nx + own.nx) * 5.0f) + ax) - ax;
                    const float ex = float(c[0]) - ax;
                    float mx = (ey * uz) - (ez * (up - ay));
                    float my = (ez * ux) - (ex * uz);
                    float mz = (ex * (up - ay)) - (ey * ux);
                    const float length = std::sqrt((mx * mx) + (my * my) + (mz * mz));
                    if (length > 0.0f) { mx = mx / length; mz = mz / length; my = my / length; }
                    const auto plane = uint16_t(block.planes.size());
                    if (neighbor != self) {
                        for (unsigned j = 0; j < 3; ++j)
                            if (block.links[neighbor][1 + j] == self) block.links[neighbor][1 + j] = uint16_t(plane | 0x8000);
                        const float side = other.d + ((float(o[0]) * other.nx) + (float(o[1]) * other.ny) + (float(o[2]) * other.nz));
                        if (side < 0.0f) block.edgeMask[t] |= uint8_t(1u << k);
                        if (((other.nz * own.nz) + ((own.nx * other.nx) + (own.ny * other.ny))) < 0.0f) {
                            float rise = ey;
                            if (cy < ay) rise = -ey;
                            if (rise <= std::sqrt((ex * ex) + (ez * ez))) block.faces[t].flags |= uint8_t((1u << k) * 4);
                        }
                    } else {
                        block.edgeMask[t] |= uint8_t(1u << k);
                    }
                    block.links[t][1 + k] = plane;
                    block.planes.push_back({mx, my, mz, -((ax * mx) + (ay * my) + (az * mz))});
                    if (block.planes.size() > 0x7FFF) throw std::runtime_error("Collision plane index overflow");
                }
            }
        }
        return block.planes.size();
    }

    static std::shared_ptr<const TrackCollision> load(const char *collisionPath, const OriginalMath &math) {
        std::ifstream input(collisionPath, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        if (!input.eof() && !input) throw std::runtime_error("Cannot read native collision data");
        size_t at = 0;
        auto take = [&](size_t count) { if (at + count > bytes.size()) throw std::runtime_error("Truncated collision data");
                                         const auto *p = bytes.data() + at; at += count; return p; };
        auto u8 = [&]() { return *take(1); };
        auto u16 = [&]() { const auto *p = take(2); return uint16_t(p[0] | p[1] << 8); };
        auto s16 = [&]() { return int16_t(u16()); };
        auto u32 = [&]() { const auto *p = take(4); return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; };
        auto align = [&]() { while (at % 4) if (u8()) throw std::runtime_error("Nonzero collision padding"); };
        if (bytes.size() < 8 || std::memcmp(take(8), "JFGCOL1\0", 8)) throw std::runtime_error("Wrong native collision format");
        auto result = std::make_shared<TrackCollision>();
        result->math = math;
        result->level = u32(); result->geometry = u32();
        const auto blockCount = u32(), textureSlots = u32();
        result->polyCapacity = u32(); result->edgeCapacity = u32(); result->noHits = u32() != 0;
        for (auto &value : result->extents) value = s16();
        if (result->level != 21 || result->geometry != 17 || blockCount != 13 || textureSlots != 45 || result->polyCapacity != 120 ||
            result->edgeCapacity != 80 || result->noHits)
            throw std::runtime_error("Unsupported native collision profile");
        for (uint32_t i = 0; i < textureSlots; ++i) result->surfaces.push_back(u8());
        align();
        for (uint32_t b = 0; b < blockCount; ++b) {
            TrackBlock block;
            block.box = {s16(), s16(), s16(), s16(), s16(), s16()};
            const auto vertexCount = u32(), triangleCount = u32(), batchCount = u32();
            if (!vertexCount || vertexCount > 256 * 64 || !triangleCount || triangleCount > 0x7000 || !batchCount || batchCount > 4096)
                throw std::runtime_error("Unbounded collision block");
            if (block.box.minX >= block.box.maxX || block.box.minY >= block.box.maxY || block.box.minZ >= block.box.maxZ)
                throw std::runtime_error("Invalid collision block box");
            for (uint32_t i = 0; i < vertexCount; ++i) {
                std::array<int16_t, 3> v{s16(), s16(), s16()};
                if (v[0] < block.box.minX || v[0] > block.box.maxX || v[1] < block.box.minY || v[1] > block.box.maxY ||
                    v[2] < block.box.minZ || v[2] > block.box.maxZ)
                    throw std::runtime_error("Collision vertex outside its block");
                block.vertices.push_back(v);
            }
            align();
            for (uint32_t i = 0; i < triangleCount; ++i) block.faces.push_back({u8(), {u8(), u8(), u8()}});
            for (uint32_t i = 0; i <= batchCount; ++i) {
                TrackBatch batch{u8(), 0, 0, 0};
                if (u8()) throw std::runtime_error("Nonzero collision batch padding");
                batch.firstVertex = u16(); batch.firstTriangle = u16();
                if (u16()) throw std::runtime_error("Nonzero collision batch padding");
                batch.flags = u32();
                block.batches.push_back(batch);
            }
            for (uint32_t i = 0; i < batchCount; ++i) {
                const auto &batch = block.batches[i], &next = block.batches[i + 1];
                if (batch.texture >= textureSlots || batch.firstTriangle > next.firstTriangle || batch.firstVertex > next.firstVertex ||
                    (i == 0 && batch.firstTriangle != 0))
                    throw std::runtime_error("Invalid collision batch ranges");
                for (size_t t = batch.firstTriangle; t < next.firstTriangle; ++t)
                    for (auto index : block.faces[t].vertex)
                        if (size_t(batch.firstVertex) + index >= next.firstVertex) throw std::runtime_error("Collision face outside its batch");
            }
            if (block.batches.back().firstTriangle != triangleCount || block.batches.back().firstVertex > vertexCount)
                throw std::runtime_error("Collision batch sentinel differs");
            for (uint32_t i = 0; i < triangleCount; ++i) {
                std::array<uint16_t, 4> link{u16(), u16(), u16(), u16()};
                if (link[0] != i) throw std::runtime_error("Unsupported face plane order");
                for (unsigned k = 1; k < 4; ++k) if (link[k] >= triangleCount) throw std::runtime_error("Collision neighbor outside block");
                block.links.push_back(link);
            }
            computeMasks(block);
            computePlanes(block, result->noHits);
            result->blocks.push_back(std::move(block));
        }
        if (at != bytes.size()) throw std::runtime_error("Trailing native collision payload");
        return result;
    }

    // getXZCompareMask(box, minX, minZ, maxX, maxZ): query clamped to the box.
    static uint32_t xzCompareMask(const TrackBox &box, int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ) {
        if (maxX < box.minX) maxX = box.minX;
        if (minX < box.minX) minX = box.minX;
        if (maxZ < box.minZ) maxZ = box.minZ;
        if (minZ < box.minZ) minZ = box.minZ;
        if (box.maxX < maxX) maxX = box.maxX;
        if (box.maxX < minX) minX = box.maxX;
        if (box.maxZ < maxZ) maxZ = box.maxZ;
        if (box.maxZ < minZ) minZ = box.maxZ;
        uint32_t result = 0, bit = 1;
        int32_t cell = ((box.maxX - box.minX) >> 4) + 1, upper = cell + box.minX, lower = box.minX;
        do {
            if (!(upper < minX) && !(maxX < lower)) result |= bit;
            bit <<= 1; upper += cell; lower += cell;
        } while (bit < 0x10000);
        cell = ((box.maxZ - box.minZ) >> 4) + 1; upper = cell + box.minZ; lower = box.minZ;
        do {
            if (!(upper < minZ) && !(maxZ < lower)) result |= bit;
            bit <<= 1; upper += cell; lower += cell;
        } while (bit != 0);
        return result;
    }
    // getYCompareMask(box, minY, maxY).
    static uint8_t yCompareMask(const TrackBox &box, int32_t minY, int32_t maxY) {
        if (maxY < box.minY) maxY = box.minY;
        if (minY < box.minY) minY = box.minY;
        if (box.maxY < maxY) maxY = box.maxY;
        if (box.maxY < minY) minY = box.maxY;
        uint32_t result = 0, bit = 1;
        int32_t cell = ((box.maxY - box.minY) >> 3) + 1, upper = cell + box.minY, lower = box.minY;
        do {
            if (!(upper < minY) && !(maxY < lower)) result |= bit;
            bit <<= 1; upper += cell; lower += cell;
        } while (bit < 0x100);
        return uint8_t(result);
    }
};

// Mutable per-query state: PLlist/PLgrps/PLpolys/PLno and trackEdges.
class TrackQuery {
    std::shared_ptr<const TrackCollision> track_;
public:
    std::vector<PolyEntry> polys;
    std::vector<TrackEdge> edges;
    explicit TrackQuery(std::shared_ptr<const TrackCollision> track) : track_(std::move(track)) {
        if (!track_) throw std::runtime_error("Missing native collision data");
    }
    const TrackCollision &track() const { return *track_; }

    // trackMakePolylist(count, starts, ends, radii, exclude, include).
    // Returns true when the list reached the level capacity (original 1).
    bool makePolylist(size_t count, const Vec3f *starts, const Vec3f *ends, const float *radii, uint32_t exclude, uint32_t include) {
        polys.clear();
        const auto &track = *track_;
        int32_t minX = 100000, minY = 100000, minZ = 100000, maxX = -100000, maxY = -100000, maxZ = -100000;
        if (count) {
            for (size_t i = 0; i < count; ++i) {
                const int32_t sx = trunc(starts[i].x), sy = trunc(starts[i].y), sz = trunc(starts[i].z);
                const int32_t ex = trunc(ends[i].x), ey = trunc(ends[i].y), ez = trunc(ends[i].z), r = trunc(radii[i]);
                for (auto [x, y, z] : {std::array<int32_t, 3>{sx, sy, sz}, std::array<int32_t, 3>{ex, ey, ez}}) {
                    if (maxX < x + r) maxX = x + r;
                    if (x - r < minX) minX = x - r;
                    if (maxY < y + r) maxY = y + r;
                    if (y - r < minY) minY = y - r;
                    if (maxZ < z + r) maxZ = z + r;
                    if (z - r < minZ) minZ = z - r;
                }
            }
            if (maxX < minX) std::swap(minX, maxX);
            if (maxY < minY) std::swap(minY, maxY);
            if (maxZ < minZ) std::swap(minZ, maxZ);
            minX -= 5; minY -= 5; minZ -= 5; maxX += 5; maxY += 5; maxZ += 5;
        }
        struct Selected { uint32_t block, xz; uint8_t y; };
        std::vector<Selected> selected;
        for (uint32_t b = 0; b < track.blocks.size() && selected.size() < 10; ++b) {
            const auto &box = track.blocks[b].box;
            if (box.maxX + 5 < minX || maxX < box.minX - 5) continue;
            if (box.maxY + 5 < minY || maxY < box.minY - 5) continue;
            if (box.maxZ + 5 < minZ || maxZ < box.minZ - 5) continue;
            selected.push_back({b, TrackCollision::xzCompareMask(box, minX, minZ, maxX, maxZ),
                                TrackCollision::yCompareMask(box, minY, maxY)});
        }
        const uint32_t skip = exclude | 0x880;
        for (const auto &choice : selected) {
            const auto &block = track.blocks[choice.block];
            polys.push_back({true, choice.block, 0, 0});
            for (uint16_t b = 0; b < block.batchCount(); ++b) {
                const auto flags = block.batches[b].flags;
                if (!(flags & include) && (flags & skip)) continue;
                for (size_t t = block.batches[b].firstTriangle; t < block.batches[b + 1].firstTriangle; ++t) {
                    const uint32_t overlap = block.xzMask[t] & choice.xz;
                    if (!(overlap & 0xFFFF) || !(overlap & 0xFFFF0000u) || !(block.yMask[t] & choice.y)) continue;
                    polys.push_back({false, choice.block, uint16_t(t), b});
                    if (polys.size() >= track.polyCapacity) return true;
                }
            }
        }
        return false;
    }
    // func_800182C0: exposed edges of the listed faces, up to the level capacity.
    void buildEdges() {
        edges.clear();
        const auto &track = *track_;
        for (const auto &entry : polys) {
            if (entry.marker) continue;
            const auto &block = track.blocks[entry.block];
            const auto &face = block.faces[entry.triangle];
            const auto &batch = block.batches[entry.batch];
            for (unsigned k = 0; k < 3; ++k) {
                if (!(block.edgeMask[entry.triangle] & (1u << k))) continue;
                const unsigned next = k + 1 >= 3 ? 0 : k + 1;
                const auto &a = block.corner(batch, face, k), &b = block.corner(batch, face, next);
                TrackEdge edge;
                edge.p0 = {float(a[0]), float(a[1]), float(a[2])};
                edge.p1 = {float(b[0]), float(b[1]), float(b[2])};
                const float dx = edge.p1.x - edge.p0.x, dy = edge.p1.y - edge.p0.y, dz = edge.p1.z - edge.p0.z;
                edge.length = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
                if (!(edge.length > 0.0f)) continue;
                edge.block = entry.block;
                edge.dir = {dx / edge.length, dy / edge.length, dz / edge.length};
                edge.batch = entry.batch;
                edge.special = (face.flags & ((1u << k) * 4)) ? 1 : 0;
                edges.push_back(edge);
                if (edges.size() >= track.edgeCapacity) return;
            }
        }
    }
    // Shared response of func_80016EA0/func_800175A0 after a confirmed contact.
    void respond(Vec3f &end, TrackHit &result, const Vec3f &n, float distanceEnd, float offset, uint16_t flags,
                 const Vec3f *contact, float radius) const {
        const auto &math = track_->math;
        if (flags & 0x81) {
            if (flags & 1) {
                if (contact) {
                    const float r = radius + 0.01f;
                    end = {(r * n.x) + contact->x, (r * n.y) + contact->y, (r * n.z) + contact->z};
                }
            } else {
                const float k = 0.01f - distanceEnd;
                end = {end.x + (k * n.x), end.y + (k * n.y), end.z + (k * n.z)};
            }
            result.normal = n;
            result.type |= n.y >= 0.707f ? 2 : n.y <= -0.866f ? 8 : 4;
        } else if (n.y >= 0.707f) {
            end.y = (-(((end.z * n.z) + (n.x * end.x)) + offset) / n.y) + 0.01f;
            result.normal = n;
            result.type |= 2;
        } else if (n.y <= -0.866f) {
            const float k = 0.01f - distanceEnd;
            end = {end.x + (k * n.x), end.y + (k * n.y), end.z + (k * n.z)};
            result.type |= 8;
        } else {
            const float k0 = -distanceEnd;
            const Vec3f pushed{end.x + (k0 * n.x), end.y + (k0 * n.y), end.z + (k0 * n.z)};
            const float hx = end.x - pushed.x, hz = end.z - pushed.z, hy = end.y - pushed.y;
            const float c = math.cosf(int32_t(math.arctanf(hy, std::sqrt((hx * hx) + (hz * hz)))));
            if (c != 0.0f) {
                const float k = (k0 + 0.01f) / c;
                const float h = std::sqrt((n.x * n.x) + (n.z * n.z));
                end.x = end.x + (k * (n.x / h));
                end.z = end.z + (k * (n.z / h));
            } else end = pushed;
            result.type |= 4;
        }
    }
    // func_80016EA0: first listed face whose radius-offset plane is crossed
    // with the contact point inside its three edge planes.
    bool planeTest(const Vec3f &start, Vec3f &end, TrackHit &result, float radius, uint16_t flags, float tolerance) const {
        const auto &track = *track_;
        for (const auto &entry : polys) {
            if (entry.marker) continue;
            const auto &block = track.blocks[entry.block];
            const auto &link = block.links[entry.triangle];
            const auto &plane = block.planes.at(link[0]);
            const float nx = plane.nx, ny = plane.ny, nz = plane.nz, offset = plane.d - radius;
            const float distanceEnd = ((end.z * nz) + ((nx * end.x) + (ny * end.y))) + offset;
            if (!(distanceEnd < 0.0f)) continue;
            const float distanceStart = ((start.z * nz) + ((nx * start.x) + (ny * start.y))) + offset;
            if (!(distanceStart >= 0.0f)) continue;
            const float t = distanceStart / (distanceStart - distanceEnd);
            const float dx = (end.x - start.x) * t, dy = (end.y - start.y) * t, dz = (end.z - start.z) * t;
            const Vec3f contact{(start.x + dx) - (radius * nx), (start.y + dy) - (radius * ny), (start.z + dz) - (radius * nz)};
            bool inside = true;
            for (unsigned j = 0; j < 3 && inside; ++j) {
                const uint16_t reference = link[1 + j], flip = reference & 0x8000;
                const auto &edge = block.planes.at(reference ^ flip);
                float side = (((edge.nx * contact.x) + (edge.ny * contact.y)) + (edge.nz * contact.z)) + edge.d;
                if (flip) side = -side;
                if (tolerance < side) inside = false;
            }
            if (!inside) continue;
            result.distance = ((dx * dx) + (dy * dy)) + (dz * dz);
            respond(end, result, {nx, ny, nz}, distanceEnd, offset, flags, &contact, radius);
            result.push = {nx, ny, nz};
            const auto &batch = block.batches[entry.batch];
            result.surface = track.surfaces[batch.texture];
            result.surfaceFlags = batch.flags;
            return true;
        }
        return false;
    }
    // trackCylinderIntersect: ray against the infinite cylinder around an edge.
    static bool cylinderIntersect(const Vec3f &origin, const Vec3f &dir, const Vec3f &point, const Vec3f &axis, float radius,
                                  float &enter, float &leave) {
        const float rx = origin.x - point.x, ry = origin.y - point.y, rz = origin.z - point.z;
        float nx = (dir.y * axis.z) - (axis.y * dir.z), ny = (dir.z * axis.x) - (axis.z * dir.x), nz = (dir.x * axis.y) - (axis.x * dir.y);
        const float squared = (nz * nz) + ((nx * nx) + (ny * ny));
        if (squared == 0.0f) return false;
        const float length = std::sqrt(squared);
        nz = nz / length; nx = nx / length; ny = ny / length;
        float d = (nz * rz) + ((rx * nx) + (ry * ny));
        if (d < 0.0f) d = -d;
        if (!(d <= radius)) return false;
        const float ox = (ry * axis.z) - (axis.y * rz), oy = (rz * axis.x) - (axis.z * rx), oz = (rx * axis.y) - (axis.x * ry);
        const float t = -((nz * oz) + ((ox * nx) + (oy * ny))) / length;
        float px = (ny * axis.z) - (axis.y * nz), py = (nz * axis.x) - (axis.z * nx), pz = (nx * axis.y) - (axis.x * ny);
        const float plength = std::sqrt((pz * pz) + ((px * px) + (py * py)));
        px = px / plength; py = py / plength; pz = pz / plength;
        const float along = (pz * dir.z) + ((dir.x * px) + (dir.y * py));
        float s = std::sqrt((radius * radius) - (d * d)) / along;
        if (s < 0.0f) s = -s;
        enter = t - s; leave = t + s;
        return true;
    }
    // trackSphereIntersect: ray against a sphere at an edge endpoint.
    static bool sphereIntersect(const Vec3f &origin, const Vec3f &dir, const Vec3f &center, float radius, float &enter, float &leave) {
        const float dx = origin.x - center.x, dy = origin.y - center.y, dz = origin.z - center.z;
        const float b = (dir.z * dz) + ((dx * dir.x) + (dy * dir.y));
        const float b2 = b * b;
        const float c = (((dx * dx) + (dy * dy)) + (dz * dz)) - (radius * radius);
        if (!(c <= b2)) return false;
        const float root = std::sqrt(b2 - c);
        enter = -b - root; leave = -b + root;
        return true;
    }
    // func_800175A0: swept sphere against exposed edges and their endpoints.
    bool edgeTest(const Vec3f &start, Vec3f &end, TrackHit &result, float radius, uint16_t flags) const {
        const auto &track = *track_;
        const float mx = end.x - start.x, my = end.y - start.y, mz = end.z - start.z;
        const float squared = ((mx * mx) + (my * my)) + (mz * mz);
        if (!(squared > 0.0f)) return false;
        const float length = std::sqrt(squared);
        const Vec3f dir{mx / length, my / length, mz / length};
        for (const auto &edge : edges) {
            bool hit = false;
            float t = 0, leave = 0;
            Vec3f contact, delta;
            if (cylinderIntersect(start, dir, edge.p0, edge.dir, radius, t, leave) && t >= 0.0f && t <= length) {
                contact = {(dir.x * t) + start.x, (dir.y * t) + start.y, (dir.z * t) + start.z};
                const float s = ((((contact.x - edge.p0.x) * edge.dir.x) + ((contact.y - edge.p0.y) * edge.dir.y)) +
                                 ((contact.z - edge.p0.z) * edge.dir.z)) /
                                ((edge.dir.z * edge.dir.z) + ((edge.dir.x * edge.dir.x) + (edge.dir.y * edge.dir.y)));
                if (s >= 0.0f && s <= edge.length) {
                    hit = true;
                    delta = {contact.x - ((edge.dir.x * s) + edge.p0.x), contact.y - ((edge.dir.y * s) + edge.p0.y),
                             contact.z - ((edge.dir.z * s) + edge.p0.z)};
                }
            }
            for (const Vec3f *corner : {&edge.p0, &edge.p1}) {
                if (hit) break;
                if (sphereIntersect(start, dir, *corner, radius, t, leave) && t >= 0.0f && t <= length) {
                    hit = true;
                    contact = {(dir.x * t) + start.x, (dir.y * t) + start.y, (dir.z * t) + start.z};
                    delta = {contact.x - corner->x, contact.y - corner->y, contact.z - corner->z};
                }
            }
            if (!hit) continue;
            const Vec3f n{delta.x / radius, delta.y / radius, delta.z / radius};
            const float offset = -(((contact.x * n.x) + (contact.y * n.y)) + (contact.z * n.z));
            const float distanceEnd = ((end.z * n.z) + ((n.x * end.x) + (n.y * end.y))) + offset;
            if ((flags & 0x81) && (flags & 1)) {
                end = contact;
                result.normal = n;
                result.type |= n.y >= 0.707f ? 2 : n.y <= -0.866f ? 8 : 4;
            } else respond(end, result, n, distanceEnd, offset, flags, nullptr, radius);
            result.distance = t * t;
            result.push = n;
            const auto &batch = track.blocks[edge.block].batches[edge.batch];
            result.surface = track.surfaces[batch.texture];
            result.surfaceFlags = batch.flags;
            result.edgeSpecial = uint8_t(edge.special);
            return true;
        }
        return false;
    }
    // trackGetPlayerIntersect: iterative multi-sphere resolution. Returns the
    // accumulated hit mask with 0xFFFF0000 (11 passes) or 0x11110000 (squeezed).
    uint32_t playerIntersect(Vec3f &position, const Vec3f *starts, Vec3f *ends, const float *radii, const Vec3f *offsets,
                             const uint16_t *flags, TrackHit *results, size_t count, int32_t &nearest, Vec3f &accumulated) {
        accumulated = {};
        nearest = -1;
        for (size_t i = 0; i < count; ++i) results[i] = TrackHit{};
        buildEdges();
        uint32_t all = 0;
        int passes = 0;
        bool squeezed = false;
        size_t chosen = 0;
        for (;;) {
            uint32_t mask = 0;
            for (size_t i = 0; i < count; ++i) {
                if (flags[i] & 0x40) continue;
                int iterations = 0;
                for (;;) {
                    bool hit = false;
                    for (int test = 0; test < 2; ++test) {
                        const bool found = test == 0 ? planeTest(starts[i], ends[i], results[i], radii[i], flags[i], 0.0f)
                                                     : edgeTest(starts[i], ends[i], results[i], radii[i], flags[i]);
                        if (found) {
                            accumulated = {accumulated.x + results[i].push.x, accumulated.y + results[i].push.y,
                                           accumulated.z + results[i].push.z};
                            mask |= 1u << i;
                            hit = true;
                        }
                    }
                    if (!hit) break;
                    ++iterations;
                    if (results[i].surface == 3) nearest = int32_t(i);
                    if (iterations >= 11) {
                        ends[i] = starts[i]; results[i].distance = 0.0f; results[i].type |= 0x80;
                        break;
                    }
                    if ((results[i].type & 0x12) && (results[i].type & 0x48)) {
                        ends[i] = starts[i]; results[i].distance = 0.0f;
                        break;
                    }
                }
            }
            if (mask && offsets) {
                float best = 1.024e9f;
                for (size_t i = 0; i < count; ++i)
                    if ((mask & (1u << i)) && results[i].distance < best) { chosen = i; best = results[i].distance; }
                results[chosen].type |= 1;
                position = {ends[chosen].x - offsets[chosen].x, ends[chosen].y - offsets[chosen].y, ends[chosen].z - offsets[chosen].z};
                for (size_t i = 0; i < count; ++i)
                    ends[i] = {offsets[i].x + position.x, offsets[i].y + position.y, offsets[i].z + position.z};
            }
            ++passes;
            all |= mask;
            if (passes >= 11) {
                for (size_t i = 0; i < count; ++i) ends[i] = starts[i];
                mask = 0;
                all |= 0xFFFF0000u;
            } else {
                bool ceiling = false, floor = false;
                for (size_t i = 0; i < count; ++i) {
                    if (results[i].type & 0x48) ceiling = true;
                    if (results[i].type & 0x12) floor = true;
                }
                if (ceiling && floor) {
                    squeezed = true;
                    for (size_t i = 0; i < count; ++i) {
                        if (results[i].type) results[i].type |= 1;
                        ends[i] = starts[i];
                    }
                    all |= 0x11110000u;
                }
            }
            if (!mask || squeezed) break;
        }
        return all;
    }
    // trackPolyHeight: height of the first vertex of the first matching
    // special face containing (x,z); returns the batch flags or 0.
    uint32_t polyHeight(float x, float z, float &height, uint32_t mask) const {
        const auto &track = *track_;
        const int32_t ix = OriginalMath::truncate(x), iz = OriginalMath::truncate(z);
        height = -32768.0f;
        for (uint32_t b = 0; b < track.blocks.size(); ++b) {
            const auto &block = track.blocks[b];
            const auto &box = block.box;
            if (!(ix < box.maxX + 4 && box.minX - 4 < ix && iz < box.maxZ + 4 && box.minZ - 4 < iz)) continue;
            const uint32_t compare = TrackCollision::xzCompareMask(box, ix, iz, ix, iz);
            for (size_t n = 0; n < block.batchCount(); ++n) {
                const auto &batch = block.batches[n];
                if (!(batch.flags & mask)) continue;
                for (size_t t = batch.firstTriangle; t < block.batches[n + 1].firstTriangle; ++t) {
                    const uint32_t overlap = block.xzMask[t] & compare;
                    if (!(overlap >> 16) || !(overlap & 0xFFFF)) continue;
                    const auto &face = block.faces[t];
                    const auto &a = block.corner(batch, face, 0);
                    if (OriginalMath::xzInTriangle(ix, iz, a, block.corner(batch, face, 1), block.corner(batch, face, 2))) {
                        height = float(a[1]);
                        return batch.flags;
                    }
                }
            }
        }
        return 0;
    }
    // func_80019324: one Liang-Barsky slab test.
    static bool clipTest(float p, float q, float &enter, float &leave) {
        if (p > 0.0f) {
            const float r = q / p;
            if (leave < r) return false;
            if (enter < r) enter = r;
            return true;
        }
        if (p < 0.0f) {
            const float r = q / p;
            if (r < enter) return false;
            if (r < leave) leave = r;
            return true;
        }
        return !(q > 0.0f);
    }
    // trackClip3D: clips start/end in place to a box, returns the parameters.
    static bool clip3D(Vec3f &start, Vec3f &end, const Vec3f &low, const Vec3f &high, float &enter, float &leave) {
        const float dx = end.x - start.x, dz = end.z - start.z, dy = end.y - start.y;
        if (dx == 0.0f && dy == 0.0f && dz == 0.0f) {
            if (low.x <= start.x && start.x <= high.x && low.y <= start.y && start.y <= high.y && low.z <= start.z && start.z <= high.z) {
                enter = 0.0f; leave = 0.0f;
                return true;
            }
            return false;
        }
        float t0 = 0.0f, t1 = 1.0f;
        if (!(clipTest(dx, low.x - start.x, t0, t1) && clipTest(-dx, start.x - high.x, t0, t1) && clipTest(dy, low.y - start.y, t0, t1) &&
              clipTest(-dy, start.y - high.y, t0, t1) && clipTest(dz, low.z - start.z, t0, t1) && clipTest(-dz, start.z - high.z, t0, t1)))
            return false;
        if (t1 < 1.0f) end = {start.x + (t1 * dx), start.y + (t1 * dy), start.z + (t1 * dz)};
        leave = t1;
        if (t0 > 0.0f) start = {start.x + (t0 * dx), start.y + (t0 * dy), start.z + (t0 * dz)};
        enter = t0;
        return true;
    }
    struct NearestHit { Vec3f point; TrackPlane plane; float distance = 0; uint32_t flags = 0; uint8_t surface = 0; };
    // trackNearestIntersection(0, start, end, result, exclude, include) without object hit models.
    bool nearestIntersection(const Vec3f &start, const Vec3f &end, NearestHit &result, uint32_t exclude, uint32_t include) const {
        const auto &track = *track_;
        struct Candidate { uint32_t block, xz; uint8_t y; float enter; };
        std::vector<Candidate> candidates;
        for (uint32_t b = 0; b < track.blocks.size() && candidates.size() < 20; ++b) {
            const auto &box = track.blocks[b].box;
            Vec3f s = start, e = end;
            float enter = 0, leave = 0;
            if (!clip3D(s, e, {float(box.minX), float(box.minY), float(box.minZ)}, {float(box.maxX), float(box.maxY), float(box.maxZ)}, enter, leave))
                continue;
            int32_t x0 = trunc(s.x), y0 = trunc(s.y), z0 = trunc(s.z), x1 = trunc(e.x), y1 = trunc(e.y), z1 = trunc(e.z);
            if (x1 < x0) std::swap(x0, x1);
            if (y1 < y0) std::swap(y0, y1);
            if (z1 < z0) std::swap(z0, z1);
            Candidate candidate{b, TrackCollision::xzCompareMask(box, x0, z0, x1, z1), TrackCollision::yCompareMask(box, y0, y1), enter};
            candidates.push_back(candidate);
            for (size_t i = candidates.size() - 1; i > 0 && candidates[i].enter < candidates[i - 1].enter; --i) std::swap(candidates[i], candidates[i - 1]);
        }
        const float dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
        const uint32_t skip = exclude | 0xCE000880u;
        float best = 1.0f;
        bool hit = false;
        for (const auto &candidate : candidates) {
            const auto &block = track.blocks[candidate.block];
            for (size_t n = 0; n < block.batchCount(); ++n) {
                const auto &batch = block.batches[n];
                if ((batch.flags & skip) || (include && !(batch.flags & include))) continue;
                for (size_t t = batch.firstTriangle; t < block.batches[n + 1].firstTriangle; ++t) {
                    const uint32_t overlap = block.xzMask[t] & candidate.xz;
                    if (!(overlap & 0xFFFF) || !(overlap & 0xFFFF0000u) || !(block.yMask[t] & candidate.y)) continue;
                    const auto &link = block.links[t];
                    const auto &plane = block.planes.at(link[0]);
                    const float after = ((end.z * plane.nz) + ((plane.nx * end.x) + (plane.ny * end.y))) + plane.d;
                    if (!(after < 0.0f)) continue;
                    const float before = ((start.z * plane.nz) + ((plane.nx * start.x) + (plane.ny * start.y))) + plane.d;
                    if (!(before >= 0.0f)) continue;
                    const float t0 = before / (before - after);
                    const Vec3f point{start.x + (dx * t0), start.y + (dy * t0), start.z + (dz * t0)};
                    bool inside = true;
                    for (unsigned j = 0; j < 3 && inside; ++j) {
                        const uint16_t reference = link[1 + j], flip = reference & 0x8000;
                        const auto &edge = block.planes.at(reference ^ flip);
                        float side = (((edge.nx * point.x) + (edge.ny * point.y)) + (edge.nz * point.z)) + edge.d;
                        if (flip) side = -side;
                        if (side > 0.0f) inside = false;
                    }
                    if (inside && t0 < best) {
                        best = t0; hit = true;
                        result.point = point; result.plane = plane;
                        result.surface = track.surfaces[batch.texture]; result.flags = batch.flags;
                    }
                }
            }
            if (hit) break;
        }
        if (hit) {
            const float a = dx * best, b = dy * best, c = dz * best;
            result.distance = std::sqrt(((a * a) + (b * b)) + (c * c));
        } else result.distance = std::sqrt(((dx * dx) + (dy * dy)) + (dz * dz));
        return hit;
    }
    // trackGetCubeBlockList with the original 16-bit margins.
    std::vector<uint32_t> cubeBlockList(int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY, int32_t maxZ) const {
        std::vector<uint32_t> result;
        const auto &blocks = track_->blocks;
        for (uint32_t b = 0; b < blocks.size(); ++b) {
            const auto &box = blocks[b].box;
            if (box.maxX >= int16_t(minX - 4) && int16_t(int16_t(maxX) + 4) >= box.minX && box.maxZ >= int16_t(minZ - 4) &&
                int16_t(int16_t(maxZ) + 4) >= box.minZ && box.maxY >= int16_t(minY - 4) && int16_t(int16_t(maxY) + 4) >= box.minY)
                result.push_back(b);
        }
        return result;
    }
    // func_8001A990: circle of squared radius against a segment and its first endpoint.
    static bool circleTouchesEdge(float px, float pz, float ax, float az, float bx, float bz, float radiusSquared) {
        const float ex = bx - ax, ez = bz - az;
        const float length = (ex * ex) + (ez * ez);
        if (length > 0.0f) {
            const float t = (((px - ax) * ex) + ((pz - az) * ez)) / length;
            if (t >= 0.0f && t <= 1.0f) {
                const float x = px - ((t * ex) + ax), z = pz - ((t * ez) + az);
                if (((x * x) + (z * z)) <= radiusSquared) return true;
            }
        }
        const float x = px - ax, z = pz - az;
        return ((x * x) + (z * z)) <= radiusSquared;
    }
    struct Height { float height, nx, ny, nz; uint32_t flags; };
    // trackCylinderHeights with a result list: floors (ceilings when asked)
    // under a vertical cylinder, sorted by descending height, at most 20.
    std::vector<Height> cylinderHeights(float x, float z, float minY, float maxY, float radius, uint32_t exclude, bool ceilings) const {
        const auto &track = *track_;
        std::vector<Height> result;
        const int32_t ix = trunc(x), iz = trunc(z), ir = trunc(radius), iminY = trunc(minY), imaxY = trunc(maxY);
        const int32_t minX = ix - ir, maxX = ix + ir, minZ = iz - ir, maxZ = iz + ir;
        const auto low = int16_t(iminY), high = int16_t(imaxY);
        const auto blocks = cubeBlockList(minX, low, minZ, maxX, high, maxZ);
        if (blocks.empty() || blocks.size() >= 8) return result;
        for (auto b : blocks) {
            const auto &block = track.blocks[b];
            const uint32_t compare = TrackCollision::xzCompareMask(block.box, int16_t(minX), int16_t(minZ), int16_t(maxX), int16_t(maxZ));
            for (size_t n = 0; n < block.batchCount(); ++n) {
                const auto &batch = block.batches[n];
                if (batch.flags & exclude) continue;
                const uint8_t surface = (batch.flags & 0x2000) ? 2 : track.surfaces[batch.texture];
                for (size_t t = batch.firstTriangle; t < block.batches[n + 1].firstTriangle; ++t) {
                    const uint32_t overlap = block.xzMask[t] & compare;
                    const auto &plane = block.planes.at(block.links[t][0]);
                    if (!(overlap >> 16) || !(overlap & 0xFFFF)) continue;
                    if (!((!ceilings && plane.ny > 0.0f) || (ceilings && plane.ny < 0.0f))) continue;
                    const auto &face = block.faces[t];
                    const auto &a = block.corner(batch, face, 0), &c1 = block.corner(batch, face, 1), &c2 = block.corner(batch, face, 2);
                    if (a[1] < low && c1[1] < low && c2[1] < low) continue;
                    if (high < a[1] && high < c1[1] && high < c2[1]) continue;
                    bool inside = ceilings ? OriginalMath::xzInTriangle(ix, iz, c2, c1, a) : OriginalMath::xzInTriangle(ix, iz, a, c1, c2);
                    if (!inside) {
                        const float r2 = radius * radius;
                        inside = circleTouchesEdge(x, z, a[0], a[2], c1[0], c1[2], r2) || circleTouchesEdge(x, z, c1[0], c1[2], c2[0], c2[2], r2) ||
                                 circleTouchesEdge(x, z, c2[0], c2[2], a[0], a[2], r2);
                    }
                    if (!inside) continue;
                    const float height = -((((plane.nx * x) + (plane.nz * z)) + plane.d) / plane.ny);
                    result.push_back({height, plane.nx, plane.ny, plane.nz, (batch.flags & 0xFFFFFF00u) | surface});
                    if (result.size() >= 20) return sortHeights(result);
                }
            }
        }
        return sortHeights(result);
    }
    static std::vector<Height> sortHeights(std::vector<Height> heights) {
        for (bool sorted = false; !sorted;) {
            sorted = true;
            for (size_t i = 0; i + 1 < heights.size(); ++i)
                if (heights[i].height < heights[i + 1].height) { std::swap(heights[i], heights[i + 1]); sorted = false; }
        }
        return heights;
    }
private:
    static int32_t trunc(float value) {
        if (!std::isfinite(value) || std::abs(value) >= 2147483648.0f) throw std::runtime_error("Nonfinite collision query");
        return int32_t(value);
    }
};
}
