// Exercise the real parser on a private snapshot and corruptions of its inputs.
#include "adapter.h"
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char **argv) {
    try {
        if (argc != 4 && argc != 5 && argc != 6) throw std::runtime_error("usage: decoder_test RAM LIST VERTEX_BASE [FLOAT_MATRIX_BASE [hand]]");
        const uint32_t list = uint32_t(std::stoul(argv[2], nullptr, 0)) & 0x1fffffffU;
        const uint32_t vertices = uint32_t(std::stoul(argv[3], nullptr, 0)) & 0x1fffffffU;
        std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
        if (!stream || stream.tellg() != 0x800000) throw std::runtime_error("expected 8 MiB snapshot");
        std::vector<uint8_t> ram(0x800000);
        stream.seekg(0);
        stream.read(reinterpret_cast<char *>(ram.data()), ram.size());
        if (!stream) throw std::runtime_error("snapshot read failed");
        for (size_t i = 0; i < ram.size(); i += 4) {
            std::swap(ram[i], ram[i + 3]);
            std::swap(ram[i + 1], ram[i + 2]);
        }
        if (argc >= 5) {
            const bool hand = argc == 6 && std::string(argv[5]) == "hand";
            if (argc == 6 && !hand) throw std::runtime_error("Unknown rigid asset");
            const uint32_t matrices = uint32_t(std::stoul(argv[4], nullptr, 0)) & 0x1fffffffU;
            const auto decode = [&](const std::vector<uint8_t> &data) {
                return jfg::decode_static_character(data.data(), uint32_t(data.size()), list, vertices, matrices, hand);
            };
            const auto original = decode(ram);
            if (original.stats.commands != (hand ? 15U : 353U) || original.stats.triangles != (hand ? 32U : 502U) ||
                original.matrixLoads != (hand ? 0U : 37U) || original.textureLoads != (hand ? 1U : 17U)) {
                throw std::runtime_error("character command counts differ");
            }
            uint32_t rejected = 0;
            auto reject = [&](uint32_t address, uint32_t value) {
                auto data = ram;
                std::memcpy(data.data() + address, &value, 4);
                bool threw = false;
                try { decode(data); } catch (const std::runtime_error &) { threw = true; }
                if (!threw) throw std::runtime_error("accepted corrupted character fixture");
                ++rejected;
            };
            reject(list, 0xAA000000U);
            if (hand) {
                reject(list + 7 * 8, 0x01810040U); // A rigid hand cannot load bones.
                reject(list + 7 * 8 + 4, 43 * 10); // Vertex outside its own model.
                reject(matrices, 0x40000000U); // Unsupported scale.
                reject(matrices + 0x30, 0x7fc00000U); // NaN translation.
                reject(list + 3 * 8 + 4, 0x7ffff8U); // Nested list crosses RAM.
                std::cout << "{\"status\":\"passed\",\"model\":309,\"triangles\":32,\"rejected_cases\":" << rejected << "}\n";
                return 0;
            }
            reject(list + 7 * 8, 0x01840040U); // Unsupported matrix cache slot.
            reject(list + 7 * 8 + 4, 21 * 64); // Matrix beyond the loader pose.
            reject(matrices + 0x100, 0x40000000U); // Nonidentity pose.
            reject(list + 9 * 8 + 4, 0); // Select an unloaded matrix.
            reject(list + 10 * 8, 0x04270030U); // Append/billboard.
            reject(list + 10 * 8 + 4, 0x7ffffeU); // Vertex source outside model.
            reject(list + 3 * 8 + 4, 0x7ffff8U); // Texture DMA outside RAM.
            uint32_t textureDma;
            std::memcpy(&textureDma, ram.data() + list + 3 * 8 + 4, 4);
            reject((textureDma & 0x1fffffffU) + 2 * 8 + 4, 0x07fff000U); // Overlarge block.
            uint32_t polygon;
            std::memcpy(&polygon, ram.data() + list + 13 * 8 + 4, 4);
            reject(polygon & 0x1fffffffU, 0x00200000U); // Vertex index 32.
            reject(polygon & 0x1fffffffU, 0x80000102U); // Unsupported triangle flags.
            std::cout << "{\"status\":\"passed\",\"valid_lists\":1,\"rejected_cases\":" << rejected
                      << ",\"triangles\":" << original.stats.triangles << ",\"vertices_loaded\":" << original.stats.vertices
                      << ",\"texture_loads\":" << original.textureLoads << "}\n";
            return 0;
        }
        const auto original = jfg::decode_model(ram.data(), uint32_t(ram.size()), list, vertices);
        if (original.vertexCount != 4 || original.triangles.size() != 2 ||
            original.commandCount != 11 || original.rdpCommands.size() != 12) {
            throw std::runtime_error("original list counts differ");
        }
        const uint8_t expected[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (size_t t = 0; t < 2; ++t) for (size_t c = 0; c < 3; ++c) {
            if (original.triangles[t].corners[c].vertex != expected[t][c]) {
                throw std::runtime_error("original triangle indices differ");
            }
        }
        uint32_t rejected = 0;
        auto reject = [&](const char *name, std::function<void()> test) {
            bool threw = false;
            try { test(); } catch (const std::runtime_error &) { threw = true; }
            if (!threw) throw std::runtime_error(std::string("accepted invalid input: ") + name);
            ++rejected;
        };
        auto mutate = [&](uint32_t address, uint32_t value) {
            auto changed = ram;
            if (address + 4 > changed.size()) throw std::logic_error("test mutation outside RAM");
            std::memcpy(changed.data() + address, &value, 4);
            jfg::decode_model(changed.data(), uint32_t(changed.size()), list, vertices);
        };
        reject("unaligned list", [&] { jfg::decode_model(ram.data(), uint32_t(ram.size()), list + 1, vertices); });
        reject("vertex bounds", [&] { jfg::decode_model(ram.data(), uint32_t(ram.size()), list, 0x7ffff8); });
        reject("partial word view", [&] { jfg::decode_model(ram.data(), 0x7fffff, list, vertices); });
        reject("list bounds", [&] { jfg::decode_model(ram.data(), uint32_t(ram.size()), 0x800000, vertices); });
        reject("unknown opcode", [&] { mutate(list, 0xAA000000); });
        reject("missing image", [&] { mutate(list + 2 * 8, 0xE7000000); });
        reject("DMA count", [&] { mutate(list + 3 * 8, 0x07070038); });
        reject("DMA bounds", [&] { mutate(list + 3 * 8 + 4, 0x7ffff8); });
        reject("vertex count", [&] { mutate(list + 7 * 8, 0x0428003A); });
        reject("polygon count", [&] { mutate(list + 8 * 8, 0x05210030); });
        reject("vertex index", [&] {
            auto changed = ram;
            changed[((original.polygonAddress & 0x1fffffffU) + 1) ^ 3] = 4;
            jfg::decode_model(changed.data(), uint32_t(changed.size()), list, vertices);
        });
        reject("load block overflow", [&] { mutate((original.textureDmaAddress & 0x1fffffffU) + 2 * 8 + 4, 0x07ffffff); });
        reject("render tile", [&] { mutate((original.textureDmaAddress & 0x1fffffffU) + 4 * 8, 0xF5102000); });
        reject("early end", [&] { mutate(list + 7 * 8, 0xB8000000); });
        std::cout << "{\"status\":\"passed\",\"valid_lists\":1,\"rejected_cases\":" << rejected
                  << ",\"triangles\":2,\"vertices\":4}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "decoder_test: " << error.what() << '\n';
        return 1;
    }
}
