// Headless replay of a play-session recording (jfg_native_play gravacoes/*.jfgpad).
// The records drive the same PlayRun as the windowed executable, one host
// tick attempt each, so stops happen at the same attempt and host tick.
#include "play_session.h"
#include <iomanip>
#include <iostream>

using namespace jfg_native;

static std::string escaped(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[2]) == "--script") {
            const auto data = loadPlayData(argv[1]);
            const auto recording = scriptRecording(data.digest);
            PadRecorder recorder(argv[3], recording.header);
            for (const auto &record : recording.records) recorder.append(record);
            recorder.flush();
            std::cout << "{\"status\":\"written\",\"records\":" << recording.records.size() << "}\n";
            return 0;
        }
        if (argc != 3) throw std::runtime_error("usage: jfg_native_replay DATA_DIR RECORDING | DATA_DIR --script OUTPUT");
        const auto data = loadPlayData(argv[1]);
        const auto recording = readPadRecording(argv[2]);
        if (recording.header.dataDigest != data.digest) throw std::runtime_error("Recording was made with different converted data");
        PlayRun run(data, recording.header.controlMode);
        for (const auto &record : recording.records) run.step(record);
        const auto *body = run.body();
        std::cout << std::setprecision(9) << "{\"status\":\"replayed\",\"records\":" << recording.records.size()
                  << ",\"host_ticks\":" << run.session().hostTick() << ",\"control_mode\":" << unsigned(recording.header.controlMode)
                  << ",\"paused\":" << (run.paused() ? "true" : "false") << ",\"failures\":[";
        for (size_t i = 0; i < run.failures.size(); ++i) {
            const auto &f = run.failures[i];
            std::cout << (i ? "," : "") << "{\"attempt\":" << f.attempt << ",\"host_tick\":" << f.hostTick << ",\"not_ported\":"
                      << (f.notPorted ? "true" : "false") << ",\"reason\":\"" << escaped(f.technical) << "\"}";
        }
        std::cout << "]";
        if (body) std::cout << ",\"final\":{\"x\":" << body->position.x << ",\"y\":" << body->position.y << ",\"z\":" << body->position.z
                            << ",\"state\":" << unsigned(body->state568) << "}";
        std::cout << ",\"state_digest\":\"" << std::hex << run.stateDigest() << "\"}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
