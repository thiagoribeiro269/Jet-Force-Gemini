// Tests for the play host services: XInput adapter, controles.ini, pad
// recordings and the replay path shared with the windowed executable.
#include "pad_adapter.h"
#include "play_session.h"
#include <iostream>

using namespace jfg_native;

static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }

int main(int argc, char **argv) {
    try {
        check(argc >= 2 && argc <= 4, "usage: check_play WORK_DIR [DATA_DIR [CONTROLES_INI]]");
        const std::filesystem::path work = argv[1];
        check(std::filesystem::is_directory(work), "Work directory does not exist");
        unsigned adapterCases = 0, configCases = 0, recordingCases = 0, replayCases = 0, rejected = 0;
        auto reject = [&](const std::function<void()> &f) {
            try { f(); } catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Invalid play input accepted");
        };
        // XInput adapter with the default mapping.
        const auto mapping = defaultMapping();
        auto adapt = [&](HostGamepad g) { return adaptGamepad(g, mapping); };
        {
            HostGamepad g;
            g.thumbLX = 2000; g.thumbLY = -2000;  // inside the 10% radial dead zone
            auto a = adapt(g);
            check(a.pad.stickX == 0 && a.pad.stickY == 0 && a.pad.button == 0 && !a.restart, "Dead zone differs");
            g.thumbLX = 32767; g.thumbLY = 0;
            a = adapt(g);
            check(a.pad.stickX == 80 && a.pad.stickY == 0 && joyClamp(a.pad.stickX) == 65, "Full tilt differs");
            g.thumbLX = -32768; g.thumbLY = 32767;
            a = adapt(g);
            check(a.pad.stickX == -57 && a.pad.stickY == 57, "Diagonal full tilt differs");
            g = {}; g.thumbLY = 16384;  // half tilt after the dead zone: (0.5 - 0.1) / 0.9 of 80
            check(adapt(g).pad.stickY == 36, "Stick scale differs");
            ++adapterCases;
            g = {}; g.buttons = Xbox::A | Xbox::X | Xbox::LB | Xbox::Start | Xbox::DpadUp | Xbox::DpadRight;
            g.leftTrigger = 200; g.rightTrigger = 31;
            a = adapt(g);
            check(a.pad.button == (Pad::A | Pad::B | Pad::L | Pad::Start | Pad::Up | Pad::Right | Pad::R | Pad::Z) && !a.restart,
                  "Button mapping differs");
            g = {}; g.rightTrigger = 30; g.buttons = Xbox::Back | Xbox::Y;
            a = adapt(g);
            check(a.pad.button == 0 && a.restart, "Trigger threshold, unmapped Y or Back restart differs");
            ++adapterCases;
            g = {}; g.thumbRX = 16384; g.thumbRY = -16384;
            check(adapt(g).pad.button == (Pad::CRight | Pad::CDown), "Right stick C buttons differ");
            g.thumbRX = -16383; g.thumbRY = 16383;
            check(adapt(g).pad.button == 0, "Right stick C threshold differs");
            g.thumbRX = -32768; g.thumbRY = 32767;
            check(adapt(g).pad.button == (Pad::CLeft | Pad::CUp), "Right stick C buttons differ");
            ++adapterCases;
        }
        // controles.ini.
        {
            const auto m = parseMapping("# comentário\nmodo = Expert\nzona_morta = 0.2 ; desgaste\nalcance=70\n"
                                        "RT = z + reiniciar\nY = c_cima\nX = nada\nlimiar_gatilho = 100\n");
            check(m.mode == ControlMode::Expert && m.deadZone == 0.2f && m.stickRange == 70 && m.triggerThreshold == 100,
                  "Configuration values differ");
            HostGamepad g; g.rightTrigger = 101; g.buttons = Xbox::Y | Xbox::X; g.thumbLX = 32767;
            const auto a = adaptGamepad(g, m);
            check(a.pad.button == (Pad::Z | Pad::CUp) && a.restart && a.pad.stickX == 70, "Configured mapping differs");
            configCases += 2;
            reject([] { parseMapping("modo = arcade\n"); });
            reject([] { parseMapping("A = pular\n"); });
            reject([] { parseMapping("turbo = 1\n"); });
            reject([] { parseMapping("zona_morta = 2\n"); });
            reject([] { parseMapping("sem igual\n"); });
            ++configCases;
            if (argc == 4) {  // The shipped controles.ini is the default mapping.
                std::ifstream input(argv[3], std::ios::binary);
                check(bool(input), "Missing controles.ini");
                const auto shipped = parseMapping(std::string((std::istreambuf_iterator<char>(input)), {}));
                const auto base = defaultMapping();
                check(shipped.mode == base.mode && shipped.deadZone == base.deadZone && shipped.stickRange == base.stickRange &&
                      shipped.cThreshold == base.cThreshold && shipped.triggerThreshold == base.triggerThreshold, "Shipped settings differ");
                for (size_t i = 0; i < base.target.size(); ++i)
                    check(shipped.target[i].n64 == base.target[i].n64 && shipped.target[i].restart == base.target[i].restart,
                          "Shipped controles.ini differs from the default mapping");
                ++configCases;
            }
        }
        // Recording round trip and malformed files.
        {
            PadRecording recording;
            recording.header = {0x0123456789ABCDEFull, 1};
            recording.records = {{{Pad::A | Pad::CLeft, -80, 127}, RecordRestart}, {{0, 0, -128}, RecordPauseToggle}, {{Pad::Z, 5, -5}, 0}};
            const auto path = work / "check-play.jfgpad";
            {
                PadRecorder recorder(path, recording.header);
                for (const auto &record : recording.records) recorder.append(record);
                recorder.flush();
            }
            const auto back = readPadRecording(path);
            check(back.header.dataDigest == recording.header.dataDigest && back.header.controlMode == 1 &&
                  back.records.size() == 3, "Recording header differs");
            for (size_t i = 0; i < 3; ++i)
                check(back.records[i].pad == recording.records[i].pad && back.records[i].flags == recording.records[i].flags,
                      "Recording record differs");
            ++recordingCases;
            auto corrupt = [&](size_t offset, uint8_t value) {
                std::ifstream in(path, std::ios::binary);
                std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
                bytes.at(offset) = value;
                const auto bad = work / "check-play-bad.jfgpad";
                std::ofstream(bad, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
                readPadRecording(bad);
            };
            reject([&] { corrupt(0, 'X'); });           // magic
            reject([&] { corrupt(16, 2); });            // control mode
            reject([&] { corrupt(24 + 4, 0x80); });     // unknown flag
            reject([&] { corrupt(24, 0x40); });         // bit outside the standard buttons
            reject([&] { corrupt(24 + 7, 1); });        // record padding
            std::filesystem::remove(path);
            std::filesystem::remove(work / "check-play-bad.jfgpad");
            ++recordingCases;
        }
        bool original = false;
        uint64_t replayDigest = 0;
        if (argc >= 3) {
            const auto data = loadPlayData(argv[2]);
            // Replay of the movement script equals the session driven directly.
            PlayRun replay(data, 0);
            const auto script = scriptRecording(data.digest);
            for (const auto &record : script.records) check(replay.step(record), "Script replay stopped");
            NativeSession direct;
            direct.boot(data.character, data.selection, data.region, data.physics, data.collision, data.camera);
            direct.apply(movementScene(*data.region));
            uint64_t tick = 0;
            auto source = [&](uint64_t) {
                TickInput input; ActorInput actor; actor.pad = movementPad(tick++);
                input.actors.push_back({EntityHandle{1, 1}, actor}); return input;
            };
            while (tick < MovementTicks) direct.advanceNanoseconds(NativeSession::ClockScale / 60 + 1, source);
            check(direct.body({1, 1})->position == replay.body()->position && direct.camera({1, 1})->position == replay.camera()->position &&
                  direct.hostTick() == replay.session().hostTick(), "Replay and direct session diverged");
            replayDigest = replay.stateDigest();
            ++replayCases;
            // The same script through a file.
            const auto path = work / "check-play-script.jfgpad";
            {
                PadRecorder recorder(path, script.header);
                for (const auto &record : script.records) recorder.append(record);
                recorder.flush();
            }
            PlayRun fromFile(data, 0);
            for (const auto &record : readPadRecording(path).records) fromFile.step(record);
            check(fromFile.failures.empty() && fromFile.stateDigest() == replayDigest, "File replay differs");
            std::filesystem::remove(path);
            ++replayCases;
            // A stop keeps the last world and the host tick; a restart record
            // retries that tick with a new scene generation.
            PlayRun run(data, 0);
            for (int t = 0; t < 40; ++t) check(run.step({}), "Idle tick stopped");
            const auto landed = run.body()->position;
            check(!run.step({{Pad::Z, 0, 0}, 0}), "Standing shot did not stop");
            check(run.failures.size() == 1 && run.failures[0].notPorted && run.failures[0].attempt == 41 &&
                  run.failures[0].hostTick == 40 && !run.failures[0].explanation.empty(), "Stop record differs");
            check(run.body()->position == landed && run.session().hostTick() == 40, "Stop changed the world");
            check(run.step({{}, RecordRestart}), "Restart failed");
            check(run.juno().scene == 2 && run.body()->position.y > 15.0f && run.session().hostTick() == 41, "Restart differs");
            for (int t = 0; t < 30; ++t) run.step({});
            check(std::abs(run.body()->position.y + 1.99f) < 1e-4f, "Restarted Juno did not land");
            ++replayCases;
            // Host pause: ticks pass, the world stays; joyRead waits for resume.
            check(run.step({{}, RecordPauseToggle}) && run.paused(), "Pause did not start");
            const auto held = run.body()->position;
            for (int t = 0; t < 10; ++t) run.step({{0, 0, 70}, 0});
            check(run.body()->position == held && run.paused(), "Paused world moved");
            check(run.step({{0, 0, 70}, RecordPauseToggle}) && !run.paused() && !(run.body()->position == held), "Resume differs");
            ++replayCases;
            // Expert mode: C-up jumps.
            PlayRun expert(data, 1);
            for (int t = 0; t < 30; ++t) expert.step({});
            expert.step({{Pad::CUp, 0, 0}, 0});
            check(expert.body()->state568 == 3 && expert.failures.empty(), "Expert jump differs");
            ++replayCases;
            reject([&] { PlayRun bad(data, 2); });
            original = true;
        }
        std::cout << "{\"status\":\"passed\",\"adapter_cases\":" << adapterCases << ",\"config_cases\":" << configCases
                  << ",\"recording_cases\":" << recordingCases << ",\"replay_cases\":" << replayCases << ",\"rejected_cases\":" << rejected
                  << ",\"original_data\":" << (original ? "true" : "false") << ",\"replay_digest\":\"" << std::hex << replayDigest << "\"}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
