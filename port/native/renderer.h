#pragma once
#include "scene_assets.h"
#include "camera.h"
#include <memory>

namespace jfg_native {
enum class CameraPreset { Original, Wide, Character, Integration, World };
struct RenderInstance { uint32_t mesh = 0; std::vector<Matrix> bones; Matrix world = identityMatrix(); };
// Rendering consumes prepared poses. It owns no animation, input or game clock.
class NativeRenderer {
    struct State;
    std::unique_ptr<State> state_;
public:
    static constexpr unsigned Width = 640, Height = 480;
    NativeRenderer(std::shared_ptr<const AssetPackage> assets, CameraPreset camera);
    NativeRenderer(std::vector<std::shared_ptr<const AssetPackage>> catalog, CameraPreset camera);
    ~NativeRenderer();
    NativeRenderer(const NativeRenderer &) = delete;
    NativeRenderer &operator=(const NativeRenderer &) = delete;
    const std::vector<uint8_t> &draw(const std::vector<RenderInstance> &instances);
    const std::vector<uint8_t> &draw(const std::vector<RenderInstance> &instances, const WorldCamera &camera);
    uint32_t vendor() const;
    // Window presentation for the play executable; `window` is a Win32 HWND.
    // The scene is drawn into a 4:3 target fitted to the client area and
    // copied centred into the swap chain. The offscreen proof path above keeps
    // its own 640x480 targets and commands.
    void attachWindow(void *window, unsigned width, unsigned height);
    void resizeWindow(unsigned width, unsigned height);
    void present(const std::vector<RenderInstance> &instances, const WorldCamera &camera, bool vsync);
    const std::vector<uint8_t> &readPresented(unsigned &width, unsigned &height);
    const char *swapEffect() const;
    // The same 4:3 scene target without a window or swap chain, so remote
    // self-tests can check the window drawing path in sessions without a desktop.
    void prepareSceneTarget(unsigned width, unsigned height);
    void drawScene(const std::vector<RenderInstance> &instances, const WorldCamera &camera);
};
}
