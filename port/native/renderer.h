#pragma once
#include "scene_assets.h"
#include <memory>

namespace jfg_native {
enum class CameraPreset { Original, Wide, Character, Integration };
struct RenderInstance { uint32_t mesh = 0; std::vector<Matrix> bones; };
// Rendering consumes prepared poses. It owns no animation, input or game clock.
class NativeRenderer {
    struct State;
    std::unique_ptr<State> state_;
public:
    static constexpr unsigned Width = 640, Height = 480;
    NativeRenderer(std::shared_ptr<const AssetPackage> assets, CameraPreset camera);
    ~NativeRenderer();
    NativeRenderer(const NativeRenderer &) = delete;
    NativeRenderer &operator=(const NativeRenderer &) = delete;
    const std::vector<uint8_t> &draw(const std::vector<RenderInstance> &instances);
    uint32_t vendor() const;
};
}
