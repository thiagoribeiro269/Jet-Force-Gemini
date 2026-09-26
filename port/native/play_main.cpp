// Playable Forest First on Windows: Win32 window, D3D11 swap chain and an
// XInput controller adapted to raw N64 pad values. Juno runs the recovered
// movement, camera and controller reading. Whatever the original would do
// and the port does not stops the session with its reason, keeps the pad
// recording and offers a restart at the entry point.
#include "play_session.h"
#include "pad_adapter.h"
#include "renderer.h"
#include <windows.h>
#include <xinput.h>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace jfg_native;

namespace {
std::wstring widen(const std::string &text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(std::max(size, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
    return out;
}
std::string readText(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), {});
}
std::string escaped(const std::string &text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

struct Host {
    HWND window = nullptr;
    NativeRenderer *renderer = nullptr;
    bool running = true, fullscreen = false, restartRequested = false;
    WINDOWPLACEMENT placement{};
    std::string windowError;
};
Host host;

void toggleFullscreen() {
    const LONG style = GetWindowLongW(host.window, GWL_STYLE);
    if (!host.fullscreen) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(MONITORINFO);
        host.placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(host.window, &host.placement);
        GetMonitorInfoW(MonitorFromWindow(host.window, MONITOR_DEFAULTTOPRIMARY), &monitor);
        SetWindowLongW(host.window, GWL_STYLE, style & ~LONG(WS_OVERLAPPEDWINDOW));
        const auto &r = monitor.rcMonitor;
        SetWindowPos(host.window, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongW(host.window, GWL_STYLE, style | LONG(WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(host.window, &host.placement);
        SetWindowPos(host.window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    host.fullscreen = !host.fullscreen;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CLOSE: host.running = false; return 0;
    case WM_SIZE:
        // Exceptions must not cross the window procedure.
        if (host.renderer && w != SIZE_MINIMIZED) {
            try { host.renderer->resizeWindow(LOWORD(l), HIWORD(l)); }
            catch (const std::exception &error) { host.windowError = error.what(); }
        }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) host.running = false;
        else if (w == VK_F5) host.restartRequested = true;
        return 0;
    case WM_SYSKEYDOWN:
        if (w == VK_RETURN && (HIWORD(l) & KF_ALTDOWN)) { toggleFullscreen(); return 0; }
        break;
    case WM_SYSCHAR:
        if (w == VK_RETURN) return 0;
        break;
    }
    return DefWindowProcW(hwnd, message, w, l);
}

// The first connected XInput pad. Empty slots are probed every two seconds
// only: XInputGetState on a missing pad is slow.
class Controller {
    int index_ = -1;
    unsigned retry_ = 0;
public:
    bool connected() const { return index_ >= 0; }
    HostGamepad poll() {
        if (index_ < 0) {
            if (retry_) { --retry_; return {}; }
            retry_ = 120;
            for (DWORD i = 0; i < XUSER_MAX_COUNT && index_ < 0; ++i) {
                XINPUT_STATE state{};
                if (XInputGetState(i, &state) == ERROR_SUCCESS) index_ = int(i);
            }
            if (index_ < 0) return {};
        }
        XINPUT_STATE state{};
        if (XInputGetState(DWORD(index_), &state) != ERROR_SUCCESS) { index_ = -1; return {}; }
        const auto &g = state.Gamepad;
        return {g.wButtons, g.bLeftTrigger, g.bRightTrigger, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY};
    }
};

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d-%H%M%S");
    return out.str();
}

HWND createWindow(HINSTANCE instance, int clientWidth, int clientHeight, bool visible) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    windowClass.lpszClassName = L"JetForceGeminiNative";
    if (!RegisterClassExW(&windowClass)) throw std::runtime_error("Cannot register the window class");
    RECT rect{0, 0, clientWidth, clientHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Jet Force Gemini", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
    if (!window) throw std::runtime_error("Cannot create the window");
    if (visible) ShowWindow(window, SW_SHOWNORMAL);
    return window;
}

bool pumpMessages() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return false;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!host.windowError.empty()) throw std::runtime_error(host.windowError);
    return host.running;
}

void presentRun(NativeRenderer &renderer, const PlayRun &run, bool vsync) {
    const auto *camera = run.camera();
    if (!camera) throw std::runtime_error("Juno camera missing");
    renderer.present(run.session().snapshot().renderInstances(), camera->view(), vsync);
}

// Remote self-test: a hidden 640x480 window plays the movement script and
// the picture is compared with the offscreen proof path. Sessions without a
// desktop (SSH) have no swap chain; the same 4:3 scene target is then drawn
// without one. Larger client sizes check the fitted target, and a stop and a
// restart go through the host logic.
int selfTest(HINSTANCE instance, const std::filesystem::path &folder, const std::filesystem::path &report) {
    std::ostringstream json;
    try {
        const auto data = loadPlayData(folder);
        host.window = createWindow(instance, 640, 480, false);
        NativeRenderer renderer(std::vector<std::shared_ptr<const AssetPackage>>{data.character, data.region->mesh}, CameraPreset::World);
        host.renderer = &renderer;
        bool windowed = true;
        std::string swapError;
        try { renderer.attachWindow(host.window, 640, 480); }
        catch (const std::exception &error) { windowed = false; swapError = error.what(); renderer.prepareSceneTarget(640, 480); }
        PlayRun run(data, 0);
        auto show = [&] {
            if (windowed) presentRun(renderer, run, false);
            else renderer.drawScene(run.session().snapshot().renderInstances(), run.camera()->view());
        };
        uint64_t tick = 0, frames = 0;
        for (; tick < MovementTicks; ++frames) {
            pumpMessages();
            if (!run.advance(NativeSession::ClockScale / 60 + 1, [&] { return PadRecord{movementPad(tick++), 0}; }, nullptr))
                throw std::runtime_error("Scripted play stopped: " + run.failures.back().technical);
            show();
        }
        const auto view = run.camera()->view();
        std::array<uint8_t, 4> clear{};
        for (int i = 0; i < 3; ++i) clear[size_t(i)] = uint8_t(std::lround(view.clearColor[size_t(i)] * 255.0f));
        auto coverage = [&](const std::vector<uint8_t> &pixels) {
            size_t covered = 0;
            for (size_t at = 0; at + 3 < pixels.size(); at += 4)
                covered += pixels[at] != clear[0] || pixels[at + 1] != clear[1] || pixels[at + 2] != clear[2];
            return double(covered) / double(pixels.size() / 4);
        };
        unsigned width = 0, height = 0;
        const auto presented = renderer.readPresented(width, height);
        const auto offscreen = renderer.draw(run.session().snapshot().renderInstances(), view);
        const bool equal = width == NativeRenderer::Width && height == NativeRenderer::Height && presented == offscreen;
        const double baseCoverage = coverage(offscreen);
        // Fitted 4:3 targets for common client sizes.
        std::ostringstream sizes;
        bool fitted = true;
        const std::array<std::array<unsigned, 4>, 3> clients{{{1920, 1080, 1440, 1080}, {1280, 1024, 1280, 960}, {1000, 700, 932, 700}}};
        for (size_t i = 0; i < clients.size(); ++i) {
            const auto &c = clients[i];
            if (windowed) { renderer.resizeWindow(c[0], c[1]); presentRun(renderer, run, false); }
            else { renderer.prepareSceneTarget(c[0], c[1]); show(); }
            unsigned w = 0, h = 0;
            const double cover = coverage(renderer.readPresented(w, h));
            fitted = fitted && w == c[2] && h == c[3] && std::abs(cover - baseCoverage) < 0.05;
            sizes << (i ? "," : "") << "{\"client\":[" << c[0] << "," << c[1] << "],\"scene\":[" << w << "," << h << "],\"coverage\":"
                  << std::setprecision(4) << cover << "}";
        }
        const auto digest = run.stateDigest();
        // A stop keeps the world; a restart record resumes the same host tick.
        const bool stopped = !run.advance(NativeSession::ClockScale / 60 + 1, [] { return PadRecord{{Pad::R, 0, 0}, 0}; }, nullptr);
        const auto hostTick = run.session().hostTick();
        // The failed tick is still in the accumulator: zero new time retries exactly it.
        const bool restarted = run.advance(0, [] { return PadRecord{{}, RecordRestart}; }, nullptr) &&
                               run.juno().scene == 2 && run.session().hostTick() == hostTick + 1;
        show();
        const bool passed = equal && fitted && stopped && restarted && run.failures.size() == 1 && run.failures[0].notPorted;
        json << "{\"status\":\"" << (passed ? "passed" : "failed") << "\",\"window\":" << (windowed ? "true" : "false")
             << ",\"swap_chain\":\"" << (windowed ? renderer.swapEffect() : "unavailable") << "\",\"swap_chain_error\":\""
             << escaped(swapError) << "\",\"frames\":" << frames << ",\"host_ticks\":" << MovementTicks
             << ",\"scene_equals_offscreen_640x480\":" << (equal ? "true" : "false") << ",\"base_coverage\":" << std::setprecision(4)
             << baseCoverage << ",\"fitted_sizes\":[" << sizes.str() << "],\"state_digest\":\"" << std::hex << digest << std::dec
             << "\",\"stop\":\"" << escaped(run.failures.empty() ? "" : run.failures[0].technical) << "\",\"restart\":"
             << (restarted ? "true" : "false") << "}";
        std::ofstream(report) << json.str() << '\n';
        std::cout << json.str() << std::endl;
        host.renderer = nullptr;
        DestroyWindow(host.window);
        return passed ? 0 : 1;
    } catch (const std::exception &error) {
        std::ofstream(report) << "{\"status\":\"error\",\"reason\":\"" << escaped(error.what()) << "\"}\n";
        std::cerr << error.what() << std::endl;
        return 1;
    }
}

struct SessionLog {
    std::filesystem::path path;
    uint64_t discardedMilliseconds = 0;
    void write(const PlayRun &run, const PadRecordingHeader &header, bool controller) const {
        std::ofstream out(path);
        out << "Jet Force Gemini, port nativo: registro da sessão\n"
            << "dados: " << std::hex << header.dataDigest << std::dec << "\nmodo de controle: "
            << (header.controlMode ? "Expert" : "Normal") << "\ntentativas de tique: " << run.attempts()
            << "\ntiques do host: " << run.session().hostTick() << "\ntempo descartado em paradas da janela (ms): "
            << discardedMilliseconds << "\ncontrole conectado no fim: " << (controller ? "sim" : "não") << "\n";
        for (const auto &f : run.failures)
            out << "parada na tentativa " << f.attempt << ", tique " << f.hostTick << ": " << f.explanation << " [" << f.technical << "]\n";
    }
};

int play(HINSTANCE instance, const std::filesystem::path &folder) {
    const auto config = folder / "controles.ini";
    const auto mapping = std::filesystem::exists(config) ? parseMapping(readText(config)) : defaultMapping();
    const auto data = loadPlayData(folder);
    host.window = createWindow(instance, 960, 720, true);
    NativeRenderer renderer(std::vector<std::shared_ptr<const AssetPackage>>{data.character, data.region->mesh}, CameraPreset::World);
    host.renderer = &renderer;
    RECT client{};
    GetClientRect(host.window, &client);
    renderer.attachWindow(host.window, unsigned(client.right), unsigned(client.bottom));
    PlayRun run(data, uint8_t(mapping.mode));
    const auto recordings = folder / "gravacoes";
    std::filesystem::create_directories(recordings);
    const std::string stem = "sessao-" + timestamp();
    const PadRecordingHeader header{data.digest, uint8_t(mapping.mode)};
    PadRecorder recorder(recordings / (stem + ".jfgpad"), header);
    SessionLog log{recordings / (stem + ".txt")};
    Controller controller;
    bool frozen = false, askRestart = false, previousRestart = false, previousStart = false;
    std::wstring shownTitle;
    LARGE_INTEGER frequency{}, last{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&last);
    auto poll = [&] {
        const auto adapted = adaptGamepad(controller.poll(), mapping);
        PadRecord record{adapted.pad, 0};
        if (host.restartRequested || (adapted.restart && !previousRestart)) record.flags |= RecordRestart;
        if ((adapted.pad.button & Pad::Start) && !previousStart) record.flags |= RecordPauseToggle;  // host pause, no menu
        host.restartRequested = false;
        previousRestart = adapted.restart;
        previousStart = adapted.pad.button & Pad::Start;
        return record;
    };
    while (pumpMessages()) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        uint64_t elapsed = uint64_t(now.QuadPart - last.QuadPart) * NativeSession::ClockScale / uint64_t(frequency.QuadPart);
        last = now;
        if (frozen && host.restartRequested) frozen = false;
        if (!frozen) {
            if (elapsed > NativeSession::MaximumAdvanceNs) {
                // Dragging or blocking the window: the extra time is dropped
                // explicitly and counted, never simulated as a burst.
                log.discardedMilliseconds += (elapsed - NativeSession::MaximumAdvanceNs) / 1000000;
                elapsed = NativeSession::MaximumAdvanceNs;
            }
            if (!run.advance(elapsed, poll, &recorder)) {
                frozen = true;
                askRestart = true;
                recorder.flush();
                log.write(run, header, controller.connected());
            }
        }
        std::wstring title = L"Jet Force Gemini · port nativo · Forest First";
        title += controller.connected() ? L" · controle conectado" : L" · sem controle: conecte um controle Xbox";
        if (run.paused()) title += L" · PAUSADO";
        if (frozen) title += L" · PARADO: " + widen(run.failures.back().explanation);
        if (title != shownTitle) { SetWindowTextW(host.window, title.c_str()); shownTitle = title; }
        presentRun(renderer, run, true);
        if (askRestart) {
            askRestart = false;
            const auto &f = run.failures.back();
            const std::wstring text = widen(f.explanation) + L"\n\nO jogo original faria isso aqui, mas o port ainda não. "
                                      L"A sessão parou para não seguir um caminho diferente do original.\n\nDetalhe técnico: " +
                                      widen(f.technical) + L"\n\nA gravação dos comandos foi salva na pasta gravacoes.\n\n"
                                      L"Repetir: recomeça no ponto de entrada. Cancelar: sai.";
            if (MessageBoxW(host.window, text.c_str(), L"Jet Force Gemini: parada", MB_RETRYCANCEL | MB_ICONINFORMATION) == IDRETRY) {
                host.restartRequested = true;
                QueryPerformanceCounter(&last);
            } else host.running = false;
        }
    }
    recorder.flush();
    log.write(run, header, controller.connected());
    host.renderer = nullptr;
    DestroyWindow(host.window);
    return 0;
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    const std::filesystem::path folder = std::filesystem::path(std::wstring(buffer, length)).parent_path();
    try {
        if (__argc == 3 && std::string(__argv[1]) == "--autoteste") return selfTest(instance, folder, __argv[2]);
        if (__argc != 1) throw std::runtime_error("uso: jfg_native_play.exe [--autoteste RELATORIO.json]");
        return play(instance, folder);
    } catch (const std::exception &error) {
        MessageBoxW(nullptr, widen(error.what()).c_str(), L"Jet Force Gemini: erro", MB_OK | MB_ICONERROR);
        return 1;
    }
}
