// Windowless application-host integration test. No private input device access.
#include "integration_scenario.h"
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace jfg_native;
int main(int argc, char **argv) {
    try {
        if (argc != 4) throw std::runtime_error("usage: jfg_native_session SCENE SELECTOR OUTPUT_PREFIX");
        auto assets = std::make_shared<const AssetPackage>(loadAssetPackage(argv[1]));
        const auto selection = readJunoSelection(argv[2]);
        NativeSession session; session.boot(assets, selection); session.apply(integrationInitial());
        NativeRenderer renderer(assets, CameraPreset::Integration);
        std::ofstream raw(std::string(argv[3]) + ".rgba", std::ios::binary), trace(std::string(argv[3]) + ".session.json");
        if (!raw || !trace) throw std::runtime_error("Cannot create native integration evidence");
        trace << std::setprecision(12) << "{\"api\":\"D3D11\",\"vendor\":" << renderer.vendor()
              << ",\"tick_rate\":60,\"output_fps\":30,\"frames\":[";
        uint64_t previous = 0;
        for (unsigned frame = 0; frame < 180; ++frame) {
            const auto time = presentationTime(frame, 30);
            session.advanceNanoseconds(time - previous, integrationInput); previous = time;
            const auto snapshot = session.snapshot();
            const auto &pixels = renderer.draw(snapshot.renderInstances());
            raw.write(reinterpret_cast<const char *>(pixels.data()), pixels.size());
            trace << (frame ? "," : "") << "{\"frame\":" << frame << ",\"host_tick\":" << snapshot.hostTick
                  << ",\"world_tick\":" << snapshot.worldTick << ",\"generation\":" << snapshot.sceneGeneration
                  << ",\"phase\":\"" << phaseName(snapshot.phase) << "\",\"scene\":\"" << snapshot.sceneName << "\",\"entities\":[";
            for (size_t i = 0; i < snapshot.entities.size(); ++i) {
                const auto &actor = snapshot.entities[i];
                trace << (i ? "," : "") << "{\"slot\":" << actor.handle.slot << ",\"x\":" << actor.x << ",\"y\":" << actor.y
                      << ",\"z\":" << actor.z << ",\"yaw\":" << actor.yaw << ",\"clip\":" << actor.clip
                      << ",\"source_frame\":" << actor.animationFrame << "}";
            }
            trace << "]}";
        }
        session.stop(); const auto stopped = session.snapshot();
        trace << "],\"stopped\":" << (stopped.phase == SessionPhase::Stopped && stopped.entities.empty() && !stopped.assets ? "true" : "false")
              << ",\"original_level\":false,\"emulation\":false}\n";
        if (!raw || !trace) throw std::runtime_error("Failed to write native integration evidence");
        std::ofstream report(std::string(argv[3]) + ".json");
        report << "{\"status\":\"rendered_unreviewed\",\"api\":\"D3D11\",\"vendor\":" << renderer.vendor()
               << ",\"width\":640,\"height\":480,\"frame_count\":180,\"tick_rate\":60,\"output_fps\":30"
               << ",\"integration\":true,\"emulator_dependencies\":false,\"display_list_interpreter\":false}\n";
        if (!report) throw std::runtime_error("Failed to write native session report");
        std::cout << "Native session: 180 frames, two scene generations, fixed 60 Hz simulation\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
