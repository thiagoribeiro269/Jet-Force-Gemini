// Original region resources and Juno in the shared native host, without a window.
#include "region_scenario.h"
#include "integration_scenario.h"
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace jfg_native;
static void save(const std::string &path, const std::vector<uint8_t> &pixels) {
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
    if (!out) throw std::runtime_error("Cannot save native region evidence");
}
int main(int argc, char **argv) {
    try {
        if (argc != 6) throw std::runtime_error("usage: jfg_native_region CHARACTER SELECTOR REGION_MESH REGION_INFO OUTPUT_PREFIX");
        auto character = std::make_shared<const AssetPackage>(loadAssetPackage(argv[1]));
        const auto selection = readJunoSelection(argv[2]); const auto region = loadRegion(argv[3], argv[4]);
        const std::string prefix = argv[5];
        NativeSession session; session.boot(character, selection, region);
        const auto initial = regionScene(*region, *character, selection); session.apply(initial);
        NativeRenderer renderer(std::vector<std::shared_ptr<const AssetPackage>>{character, region->mesh}, CameraPreset::World);
        const auto camera = regionCamera(*region, 0); auto instances = session.snapshot().renderInstances();
        save(prefix + ".background.rgba", renderer.draw({}, camera));
        save(prefix + ".terrain.rgba", renderer.draw({instances[0]}, camera));
        save(prefix + ".character.rgba", renderer.draw({instances[1]}, camera));
        std::ofstream raw(prefix + ".rgba", std::ios::binary), trace(prefix + ".region.json");
        if (!raw || !trace) throw std::runtime_error("Cannot create native region output");
        trace << std::setprecision(12) << "{\"level\":" << region->level << ",\"geometry\":" << region->geometry
              << ",\"source_spawn\":[" << region->sourceSpawn[0] << "," << region->sourceSpawn[1] << "," << region->sourceSpawn[2]
              << "],\"model_scale\":" << region->playerScale << ",\"visual_offset_y\":" << initial.scene->entities[0].visualOffsetY
              << ",\"ground_y\":" << region->groundBelow(40, 841, 19)->height << ",\"frames\":[";
        uint64_t previous = 0;
        for (unsigned frame = 0; frame < 180; ++frame) {
            const auto time = presentationTime(frame, 30);
            session.advanceNanoseconds(time - previous, [](uint64_t) { return TickInput{}; }); previous = time;
            const auto snapshot = session.snapshot();
            const auto &pixels = renderer.draw(snapshot.renderInstances(), regionCamera(*region, double(frame) / 30));
            raw.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
            trace << (frame ? "," : "") << "{\"frame\":" << frame << ",\"tick\":" << snapshot.hostTick
                  << ",\"world_meshes\":" << (snapshot.region ? 1 : 0) << ",\"entities\":" << snapshot.entities.size()
                  << ",\"clip\":" << snapshot.entities[0].clip << ",\"source_frame\":" << snapshot.entities[0].animationFrame << "}";
        }
        session.stop(); const auto stopped = session.snapshot();
        trace << "],\"stopped\":" << (stopped.entities.empty() && !stopped.assets && !stopped.region ? "true" : "false")
              << ",\"original_region\":true,\"original_gameplay\":false,\"emulation\":false}\n";
        std::ofstream report(prefix + ".json");
        report << "{\"status\":\"rendered_unreviewed\",\"api\":\"D3D11\",\"vendor\":" << renderer.vendor()
               << ",\"width\":640,\"height\":480,\"frame_count\":180,\"region\":21,\"geometry\":17,\"terrain_triangles\":2018"
               << ",\"character_triangles\":534,\"resource_count\":2,\"output_fps\":30,\"perspective\":true"
               << ",\"emulator_dependencies\":false,\"display_list_interpreter\":false}\n";
        if (!raw || !trace || !report) throw std::runtime_error("Cannot finish native region output");
        std::cout << "Native Forest First region: 2018 terrain triangles and Juno, 180 perspective frames\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
