// Native Windows mesh preview. No console CPU, command interpreter or emulator.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <memory>
#include <iomanip>
#include "character_sequence.h"
#include "juno_sequence.h"
#include "renderer.h"

constexpr unsigned Width = 640, Height = 480;

static void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }

enum class Mode { Neutral, Clip, WideClip, Sequence, Character, JunoSelection };
struct Selection { uint32_t frame, id; double duration; };
struct Sequence { uint32_t frames = 0; std::vector<Selection> events; };
static Sequence readSequence(const char *path, const std::vector<jfg_native::Clip> &clips) {
    std::ifstream input(path, std::ios::ate);
    require(bool(input) && input.tellg() < 65536, "Cannot read bounded native sequence"); input.seekg(0);
    Sequence result;
    std::string line;
    while (std::getline(input, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream row(line); std::string first, extra;
        if (!(row >> first)) continue;
        if (!result.frames) {
            require(first == "frames" && bool(row >> result.frames) && !(row >> extra) &&
                    result.frames >= 2 && result.frames <= 180, "Invalid sequence frame budget");
            continue;
        }
        std::istringstream values(line); uint64_t frame = 0, id = 0; double duration = 0;
        require(bool(values >> frame >> id >> duration) && !(values >> extra) && frame < result.frames && id <= UINT32_MAX &&
                std::isfinite(duration) && duration >= 0 && duration <= 10 && result.events.size() < 64,
                "Invalid native selection command");
        require(result.events.empty() || frame >= result.events.back().frame, "Sequence commands are not ordered");
        require(std::any_of(clips.begin(), clips.end(), [id](const auto &clip) { return clip.id == id; }), "Sequence references an absent clip");
        result.events.push_back({uint32_t(frame), uint32_t(id), duration});
    }
    require(result.frames && !result.events.empty(), "Empty native sequence");
    return result;
}

static void render(const char *input, const char *prefix, Mode mode, const char *sequencePath, const char *selectionPath) {
    const bool animate = mode != Mode::Neutral;
    const bool wideCamera = mode == Mode::WideClip || mode == Mode::Sequence;
    auto assets = std::make_shared<const jfg_native::AssetPackage>(jfg_native::loadAssetPackage(input));
    const bool multipleClips = assets->multipleClips, rigged = assets->rigged;
    require(!animate || rigged, "Animated diagnostics require a skeleton");
    const auto &clips = assets->clips; const auto &skeleton = assets->skeleton;
    const auto textureCount = assets->textures.size(), drawCount = assets->draws.size(), vertexCount = assets->vertices.size();
    Sequence sequence;
    std::unique_ptr<jfg_native::AnimationPlayer> player;
    jfg_native::CharacterSequence commands;
    std::unique_ptr<jfg_native::CharacterController> character;
    jfg_native::JunoSequence junoCommands;
    std::unique_ptr<jfg_native::JunoAnimationController> juno;
    if (mode == Mode::Sequence) {
        require(multipleClips && sequencePath, "Selection sequence requires the multi-clip scene");
        sequence = readSequence(sequencePath, clips);
        player = std::make_unique<jfg_native::AnimationPlayer>(clips, skeleton.size(), clips.front().id);
    }
    if (mode == Mode::Character) {
        require(clips.size() >= 3 && sequencePath, "Character commands require at least the three-clip profile");
        std::ifstream inputCommands(sequencePath, std::ios::ate);
        require(bool(inputCommands) && inputCommands.tellg() < 65536, "Cannot read bounded character commands");
        inputCommands.seekg(0); commands = jfg_native::readCharacterSequence(inputCommands);
        character = std::make_unique<jfg_native::CharacterController>(clips, skeleton.size(), jfg_native::CharacterClips{1071, 1026, 1030});
    }
    if (mode == Mode::JunoSelection) {
        require(clips.size() == 9 && sequencePath && selectionPath, "Original selection requires the nine-clip profile and private table");
        const auto selection = jfg_native::readJunoSelection(selectionPath);
        std::ifstream commandsFile(sequencePath, std::ios::ate);
        require(bool(commandsFile) && commandsFile.tellg() < 65536, "Cannot read bounded Juno selection commands");
        commandsFile.seekg(0); junoCommands = jfg_native::readJunoSequence(commandsFile, selection, clips);
        juno = std::make_unique<jfg_native::JunoAnimationController>(selection, clips, skeleton.size());
    }
    const auto camera = character || juno ? jfg_native::CameraPreset::Character : wideCamera ? jfg_native::CameraPreset::Wide : jfg_native::CameraPreset::Original;
    jfg_native::NativeRenderer renderer(assets, camera);
    std::ofstream raw(std::string(prefix) + ".rgba", std::ios::binary);
    require(bool(raw), "Cannot create frame stream");
    const uint32_t frameCount = juno ? junoCommands.frames : character ? commands.frames : player ? sequence.frames : animate ? uint32_t(clips.front().keys.size() * 2 + 1) : 1;
    std::vector<std::string> requestTrace, stateTrace;
    size_t eventIndex = 0;
    uint32_t accepted = 0;
    float maximumJump = 0;
    uint32_t colored = 0, minColored = Width * Height, maxColored = 0;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        jfg_native::RenderInstance instance;
        if (rigged) {
            if (juno) {
                if (eventIndex < junoCommands.events.size() && junoCommands.events[eventIndex].frame == frame) {
                    const auto &event = junoCommands.events[eventIndex++];
                    const auto before = jfg_native::composePose(skeleton, juno->animation().current());
                    const bool changed = juno->motion(event.state, event.startFraction, event.blendSeconds);
                    const auto after = jfg_native::composePose(skeleton, juno->animation().current());
                    float jump = 0;
                    for (size_t b = 0; b < before.size(); ++b) for (size_t i = 0; i < 16; ++i)
                        jump = std::max(jump, std::abs(before[b][i] - after[b][i]));
                    require(event.blendSeconds == 0 || jump < 0.00001f, "Juno selection caused an instantaneous pose jump");
                    maximumJump = std::max(maximumJump, jump); accepted += changed;
                    const auto &selection = juno->selection();
                    std::ostringstream row;
                    row << "{\"frame\":" << frame << ",\"requested\":" << selection.requested << ",\"local\":" << selection.local
                        << ",\"id\":" << selection.clip << ",\"profile\":" << selection.transitionProfile
                        << ",\"accepted\":" << (changed ? "true" : "false") << ",\"start_fraction\":" << event.startFraction
                        << ",\"blend_seconds\":" << event.blendSeconds << ",\"instant_matrix_delta\":" << jump << "}";
                    requestTrace.push_back(row.str());
                }
                const auto &animation = juno->animation(); const auto &selection = juno->selection();
                std::ostringstream row;
                row << std::setprecision(12) << "{\"frame\":" << frame << ",\"target\":" << animation.id()
                    << ",\"source_frame\":" << animation.frame() << ",\"weight\":" << animation.weight()
                    << ",\"transitioning\":" << (animation.transitioning() ? "true" : "false")
                    << ",\"requested\":" << selection.requested << ",\"local\":" << selection.local
                    << ",\"profile\":" << selection.transitionProfile << "}";
                stateTrace.push_back(row.str());
            }
            if (character) {
                if (eventIndex < commands.events.size() && commands.events[eventIndex].frame == frame) {
                    const auto &event = commands.events[eventIndex++];
                    const auto before = jfg_native::composePose(skeleton, character->animation().current());
                    const auto beforeWorld = character->world();
                    const bool changed = character->command(event.input);
                    const auto after = jfg_native::composePose(skeleton, character->animation().current());
                    float jump = 0;
                    for (size_t b = 0; b < before.size(); ++b) for (size_t i = 0; i < 16; ++i)
                        jump = std::max(jump, std::abs(before[b][i] - after[b][i]));
                    require(jump < 0.00001f && beforeWorld == character->world(), "Character input teleported the pose or world transform");
                    maximumJump = std::max(maximumJump, jump); accepted += changed;
                    std::ostringstream row;
                    row << "{\"frame\":" << frame << ",\"x\":" << event.input.x << ",\"z\":" << event.input.z
                        << ",\"low\":" << (event.input.low ? "true" : "false") << ",\"accepted\":" << (changed ? "true" : "false")
                        << ",\"instant_matrix_delta\":" << jump << "}";
                    requestTrace.push_back(row.str());
                }
                const auto &animation = character->animation();
                std::ostringstream row;
                row << std::setprecision(12) << "{\"frame\":" << frame << ",\"state\":\"" << jfg_native::stateName(character->state())
                    << "\",\"target\":" << animation.id() << ",\"source_frame\":" << animation.frame()
                    << ",\"weight\":" << animation.weight() << ",\"transitioning\":" << (animation.transitioning() ? "true" : "false")
                    << ",\"x\":" << character->x() << ",\"z\":" << character->z() << ",\"yaw\":" << character->yaw()
                    << ",\"speed\":" << character->speed() << "}";
                stateTrace.push_back(row.str());
            }
            if (player) {
                while (eventIndex < sequence.events.size() && sequence.events[eventIndex].frame == frame) {
                    const auto &event = sequence.events[eventIndex++];
                    const auto before = jfg_native::composePose(skeleton, player->current());
                    const bool changed = player->select(event.id, event.duration);
                    const auto after = jfg_native::composePose(skeleton, player->current());
                    float jump = 0;
                    for (size_t b = 0; b < before.size(); ++b) for (size_t i = 0; i < 16; ++i)
                        jump = std::max(jump, std::abs(before[b][i] - after[b][i]));
                    require(event.duration == 0 || jump < 0.00001f, "Native selection caused an instantaneous pose jump");
                    maximumJump = std::max(maximumJump, jump); accepted += changed;
                    std::ostringstream row;
                    row << "{\"frame\":" << frame << ",\"id\":" << event.id << ",\"accepted\":" << (changed ? "true" : "false")
                        << ",\"duration\":" << event.duration << ",\"instant_matrix_delta\":" << jump << "}";
                    requestTrace.push_back(row.str());
                }
                std::ostringstream row;
                row << "{\"frame\":" << frame << ",\"target\":" << player->id() << ",\"source_frame\":" << player->frame()
                    << ",\"weight\":" << player->weight() << ",\"transitioning\":" << (player->transitioning() ? "true" : "false") << "}";
                stateTrace.push_back(row.str());
            }
            auto matrices = juno ? jfg_native::composePose(skeleton, juno->animation().current()) :
                character ? jfg_native::composePose(skeleton, character->animation().current()) :
                player ? jfg_native::composePose(skeleton, player->current()) :
                jfg_native::pose(skeleton, animate ? &clips.front() : nullptr, float(frame) * 0.5f);
            if (character) for (auto &matrix : matrices) matrix = jfg_native::multiply(matrix, character->world());
            instance.bones = std::move(matrices);
        }
        const auto &pixels = renderer.draw({instance});
        colored = 0;
        for (size_t p = 0; p < pixels.size(); p += 4) colored += bool(pixels[p] | pixels[p + 1] | pixels[p + 2]);
        require(colored > 1000, "Empty native framebuffer");
        minColored = std::min(minColored, colored); maxColored = std::max(maxColored, colored);
        raw.write(reinterpret_cast<const char *>(pixels.data()), pixels.size()); require(bool(raw), "Cannot write readback");
        if (player) player->advance(1.0 / 30);
        if (character) character->advance(1.0 / 30);
        if (juno) juno->advance(1.0 / 30);
    }
    if (player || character || juno) {
        std::ofstream trace(std::string(prefix) + ".controller.json");
        require(bool(trace), "Cannot create controller trace");
        trace << "{\"requests\":[";
        for (size_t i = 0; i < requestTrace.size(); ++i) trace << (i ? "," : "") << requestTrace[i];
        trace << "],\"states\":[";
        for (size_t i = 0; i < stateTrace.size(); ++i) trace << (i ? "," : "") << stateTrace[i];
        trace << "]}\n"; require(bool(trace), "Cannot write controller trace");
    }
    std::ofstream report(std::string(prefix) + ".json");
    report << "{\"status\":\"rendered_unreviewed\",\"api\":\"D3D11\",\"vendor\":" << renderer.vendor()
           << ",\"width\":" << Width << ",\"height\":" << Height << ",\"triangles\":" << vertexCount / 3
           << ",\"draws\":" << drawCount << ",\"textures\":" << textureCount << ",\"colored_pixels\":" << colored
           << ",\"frame_count\":" << frameCount << ",\"animation_id\":" << (juno ? 1019 : character ? 1071 : animate ? clips.front().id : 0)
           << ",\"source_keyframes\":" << (juno ? 50 : character ? 3 : clips.empty() ? 0 : clips.front().keys.size()) << ",\"output_fps\":30,\"source_step\":0.5"
           << ",\"sequence\":" << (player ? "true" : "false") << ",\"clip_count\":" << clips.size()
           << ",\"character_commands\":" << (character ? "true" : "false")
           << ",\"juno_selection\":" << (juno ? "true" : "false")
           << ",\"selection_requests\":" << requestTrace.size() << ",\"accepted_switches\":" << accepted
           << ",\"instant_pose_max_matrix_delta\":" << maximumJump
           << ",\"camera\":\"" << (juno ? "juno_selection" : character ? "character_path" : wideCamera ? "transition_wide" : "original_native_proof") << "\""
           << ",\"min_colored_pixels\":" << minColored << ",\"max_colored_pixels\":" << maxColored
           << ",\"emulator_dependencies\":false,\"display_list_interpreter\":false,\"animated\":" << (animate ? "true" : "false") << "}\n";
    require(bool(report), "Cannot write report");
    std::cout << "Native D3D11: " << frameCount << " frames, " << vertexCount / 3 << " triangles each\n";
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        Mode mode = Mode::Neutral; const char *sequence = nullptr, *selection = nullptr;
        if (argc == 4 && std::string(argv[3]) == "--animate") mode = Mode::Clip;
        else if (argc == 4 && std::string(argv[3]) == "--animate-wide") mode = Mode::WideClip;
        else if (argc == 5 && std::string(argv[3]) == "--sequence") { mode = Mode::Sequence; sequence = argv[4]; }
        else if (argc == 5 && std::string(argv[3]) == "--character") { mode = Mode::Character; sequence = argv[4]; }
        else if (argc == 6 && std::string(argv[3]) == "--juno-selection") { mode = Mode::JunoSelection; sequence = argv[4]; selection = argv[5]; }
        else require(argc == 3, "usage: jfg_native_preview SCENE OUTPUT_PREFIX [--animate | --animate-wide | --sequence FILE | --character FILE | --juno-selection FILE TABLE]");
        render(argv[1], argv[2], mode, sequence, selection); return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
