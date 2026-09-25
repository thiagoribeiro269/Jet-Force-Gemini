// One controlled JFG model/texture proof. This is not the game's camera or a
// general-purpose headless RT64 frontend. It never creates a window/swapchain.
#include "adapter.h"

#include "gbi/rt64_gbi_f3d.h"
#include "hle/rt64_interpreter.h"
#include "hle/rt64_state.h"
#include "render/rt64_render_target_manager.h"
#include "shared/rt64_f3d_defines.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace plume {
std::unique_ptr<RenderInterface> CreateD3D12Interface();
}

namespace {
constexpr uint32_t Width = 320;
constexpr uint32_t Height = 240;
constexpr uint32_t RdramSize = 8U * 1024U * 1024U;
constexpr uint32_t ColorAddress = 0x700000;
constexpr uint32_t DepthAddress = 0x740000;
constexpr uint32_t ScratchAddress = 0x770000;
constexpr uint64_t DefaultTexturePool = 64ULL * 1024ULL * 1024ULL;

void checkInterrupts() {}

struct Options {
    std::string ram;
    std::string out;
    uint32_t list = 0;
    uint32_t vertexBase = 0;
    uint32_t matrixBase = 0; // Nonzero selects the explicit static character path.
    uint32_t handList = 0, handVertices = 0, handMatrix = 0;
    uint64_t texturePool = DefaultTexturePool;
};

uint32_t parseAddress(const std::string &arg) {
    size_t used = 0;
    unsigned long value = std::stoul(arg, &used, 0);
    if ((used != arg.size()) || (value > UINT32_MAX)) {
        throw std::runtime_error("Invalid 32-bit address: " + arg);
    }
    const uint32_t address = uint32_t(value);
    const uint32_t physical = ((address >= 0x80000000U) && (address < 0xc0000000U))
        ? (address & 0x1fffffffU) : address;
    if (physical >= RdramSize) {
        throw std::runtime_error("Address outside 8 MiB RDRAM: " + arg);
    }
    return physical;
}

Options parseOptions(int argc, char **argv) {
    Options options;
    bool hasList = false;
    bool hasVertexBase = false;
    for (int i = 1; i < argc; i++) {
        const std::string key = argv[i];
        if ((i + 1) >= argc) {
            throw std::runtime_error("Missing value for " + key);
        }
        const std::string value = argv[++i];
        if (key == "--ram") options.ram = value;
        else if (key == "--out") options.out = value;
        else if (key == "--list") { options.list = parseAddress(value); hasList = true; }
        else if (key == "--vertex-base") { options.vertexBase = parseAddress(value); hasVertexBase = true; }
        else if (key == "--matrix-base") { options.matrixBase = parseAddress(value); }
        else if (key == "--hand-list") { options.handList = parseAddress(value); }
        else if (key == "--hand-vertices") { options.handVertices = parseAddress(value); }
        else if (key == "--hand-matrix") { options.handMatrix = parseAddress(value); }
        else if (key == "--texture-pool-mib") {
            const unsigned long amount = std::stoul(value);
            if ((amount == 0) || (amount > 64)) throw std::runtime_error("Texture pool must be 1..64 MiB");
            options.texturePool = uint64_t(amount) * 1024ULL * 1024ULL;
        }
        else throw std::runtime_error("Unknown option: " + key);
    }
    if (options.ram.empty() || options.out.empty() || !hasList || !hasVertexBase) {
        throw std::runtime_error("Usage: jfg-rt64-headless --ram PATH --out PATH --list ADDRESS --vertex-base ADDRESS [--texture-pool-mib 1..64]");
    }
    if ((options.list + 8 > RdramSize) || (options.vertexBase + 64 > RdramSize)) {
        throw std::runtime_error("Display list or vertex base crosses RDRAM boundary");
    }
    if ((options.handList || options.handVertices || options.handMatrix) &&
        !(options.matrixBase && options.handList && options.handVertices && options.handMatrix)) {
        throw std::runtime_error("Hand attachment requires body and all three hand addresses");
    }
    return options;
}

std::vector<uint8_t> readWordSwappedRam(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != std::streamoff(RdramSize)) {
        throw std::runtime_error("Expected an exact 8 MiB big-endian RDRAM image");
    }
    std::vector<uint8_t> ram(RdramSize);
    input.seekg(0);
    input.read(reinterpret_cast<char *>(ram.data()), ram.size());
    if (!input) throw std::runtime_error("Cannot read RDRAM image");
    for (size_t i = 0; i < ram.size(); i += 4) {
        std::swap(ram[i], ram[i + 3]);
        std::swap(ram[i + 1], ram[i + 2]);
    }
    return ram;
}

void setupControlledCamera(RT64::State &state, bool character) {
    // Model 35's observed local bounds are x[-35,39], y[0,126], z=0.
    // This orthographic view centers that quad, with generous black margins.
    auto &rsp = *state.rsp;
    rsp.modelMatrixStack[0] = hlslpp::float4x4::identity();
    auto projection = hlslpp::float4x4::identity();
    projection[0][0] = 1.0f / 80.0f;
    projection[1][1] = -1.0f / 80.0f;
    projection[2][2] = 1.0f / 1024.0f;
    projection[3][0] = -2.0f / 80.0f;
    projection[3][1] = 63.0f / 80.0f;
    if (character) {
        // Assembled neutral skeleton: equal pixel scale, looking toward the
        // front with 20 degrees of yaw. RT64's viewport already flips Y.
        constexpr float cosine = -0.939692621f, sine = -0.342020143f;
        projection[0][0] = cosine / 180.0f;
        projection[2][0] = sine / 180.0f;
        projection[1][1] = 1.0f / 135.0f;
        // Camera Z is reversed relative to screen X/Y. The previous positive
        // determinant projection disagreed with face winding: a no-cull
        // diagnostic exposed the back while culling showed the front through
        // it. Keep depth ordering consistent with the chosen front faces.
        projection[0][2] = sine / 1024.0f;
        projection[2][2] = -cosine / 1024.0f;
        projection[3][0] = 0.0f;
        projection[3][1] = -113.0f / 135.0f;
    }
    rsp.viewMatrixStack[0] = hlslpp::float4x4::identity();
    rsp.projMatrixStack[0] = projection;
    rsp.viewProjMatrixStack[0] = projection;
    rsp.invViewProjMatrixStack[0] = hlslpp::inverse(projection);
    rsp.extended.viewMatrix = hlslpp::float4x4::identity();
    rsp.extended.projMatrix = hlslpp::float4x4::identity();
    rsp.extended.viewProjMatrix = hlslpp::float4x4::identity();
    rsp.extended.invViewMatrix = hlslpp::float4x4::identity();
    rsp.extended.invProjMatrix = hlslpp::float4x4::identity();
    rsp.extended.invViewProjMatrix = hlslpp::float4x4::identity();
    rsp.projectionMatrixChanged = true;
    rsp.projectionMatrixInversed = true;
    rsp.modelViewProjChanged = true;
    rsp.viewportChanged = true;
    auto &viewport = rsp.viewportStack[0];
    // RSP::setViewport divides the native Z fields by DepthRange (1024).
    constexpr float zMid = 511.0f / 1024.0f;
    viewport.scale = hlslpp::float3(Width / 2.0f, Height / 2.0f, zMid);
    viewport.translate = hlslpp::float3(Width / 2.0f, Height / 2.0f, zMid);
}

std::vector<uint8_t> readColorTarget(plume::RenderDevice *device, plume::RenderTexture *texture) {
    const auto phase = [](const char *name) {
        std::cerr << "jfg-rt64-headless phase=" << name << '\n' << std::flush;
    };
    constexpr uint32_t PixelBytes = 4;
    const uint32_t pitch = (Width * PixelBytes + 255U) & ~255U;
    phase("readback_create_buffer_begin");
    auto readback = device->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(uint64_t(pitch) * Height));
    if (!readback) throw std::runtime_error("Cannot create GPU readback buffer");
    phase("readback_create_buffer_done");
    phase("readback_worker_begin");
    RT64::RenderWorker worker(device, "JFG One-Shot Readback", plume::RenderCommandListType::DIRECT);
    phase("readback_worker_done");
    worker.commandList->begin();
    phase("readback_barrier_begin");
    worker.commandList->barriers(plume::RenderBarrierStage::COPY, plume::RenderTextureBarrier(texture, plume::RenderTextureLayout::COPY_SOURCE));
    phase("readback_barrier_done");
    phase("readback_copy_begin");
    worker.commandList->copyTextureRegion(
        plume::RenderTextureCopyLocation::PlacedFootprint(readback.get(), plume::RenderFormat::R8G8B8A8_UNORM, Width, Height, 1, pitch / PixelBytes),
        plume::RenderTextureCopyLocation::Subresource(texture));
    phase("readback_copy_done");
    worker.commandList->end();
    phase("readback_execute_begin");
    worker.execute();
    phase("readback_execute_done");
    phase("readback_wait_begin");
    worker.wait();
    phase("readback_wait_done");
    phase("readback_map_begin");
    const auto *mapped = static_cast<const uint8_t *>(readback->map());
    if (!mapped) throw std::runtime_error("Cannot map GPU readback buffer");
    phase("readback_map_done");
    std::vector<uint8_t> result(uint64_t(Width) * Height * PixelBytes);
    for (uint32_t y = 0; y < Height; y++) {
        std::memcpy(result.data() + uint64_t(y) * Width * PixelBytes, mapped + uint64_t(y) * pitch, Width * PixelBytes);
    }
    readback->unmap();
    phase("readback_unmap_done");
    return result;
}

void writeFile(const std::string &path, const void *data, size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create output: " + path);
    output.write(static_cast<const char *>(data), size);
    if (!output) throw std::runtime_error("Cannot write output: " + path);
}

bool overlaps(uint32_t a, uint32_t aSize, uint32_t b, uint32_t bSize) {
    return uint64_t(a) < uint64_t(b) + bSize && uint64_t(b) < uint64_t(a) + aSize;
}

json inspectWorkload(const RT64::Workload &workload, bool includeShader) {
    const auto &draw = workload.drawData;
    json result = {
        {"framebuffer_pairs", workload.fbPairCount},
        {"game_calls", workload.gameCallCount},
        {"pos_floats_count", draw.posFloats.size()},
        {"pos_screen_count", draw.posScreen.size()},
        {"tc_floats_count", draw.tcFloats.size()},
        {"norm_col_bytes_count", draw.normColBytes.size()},
        {"face_indices_count", draw.faceIndices.size()},
        {"call_tiles_count", draw.callTiles.size()},
        {"pairs", json::array()},
        {"pos_floats_sample", json::array()},
        {"pos_screen_sample", json::array()},
        {"tc_floats_sample", json::array()},
        {"norm_col_bytes_sample", json::array()}
    };
    for (size_t i = 0; i < std::min<size_t>(draw.posFloats.size(), 24); i++) result["pos_floats_sample"].push_back(draw.posFloats[i]);
    for (size_t i = 0; i < std::min<size_t>(draw.posScreen.size(), 8); i++) {
        const auto &p = draw.posScreen[i];
        result["pos_screen_sample"].push_back({float(p.x), float(p.y), float(p.z)});
    }
    for (size_t i = 0; i < std::min<size_t>(draw.tcFloats.size(), 16); i++) result["tc_floats_sample"].push_back(draw.tcFloats[i]);
    for (size_t i = 0; i < std::min<size_t>(draw.normColBytes.size(), 32); i++) result["norm_col_bytes_sample"].push_back(uint32_t(draw.normColBytes[i]));
    for (uint32_t f = 0; f < workload.fbPairCount; f++) {
        const auto &pair = workload.fbPairs[f];
        json pairJson = {
            {"index", f},
            {"color_address", pair.colorImage.address},
            {"color_width", pair.colorImage.width},
            {"projection_count", pair.projectionCount},
            {"game_call_count", pair.gameCallCount},
            {"depth_read", pair.depthRead},
            {"depth_write", pair.depthWrite},
            {"fill_rect_only", pair.fillRectOnly},
            {"draw_color_rect", {pair.drawColorRect.ulx, pair.drawColorRect.uly, pair.drawColorRect.lrx, pair.drawColorRect.lry}},
            {"projections", json::array()}
        };
        for (uint32_t p = 0; p < pair.projectionCount; p++) {
            const auto &projection = pair.projections[p];
            json projectionJson = {
                {"type", static_cast<uint32_t>(projection.type)},
                {"game_call_count", projection.gameCallCount},
                {"calls", json::array()}
            };
            for (uint32_t c = 0; c < projection.gameCallCount; c++) {
                const auto &call = projection.gameCalls[c];
                json callJson = {{"triangles", call.callDesc.triangleCount}};
                if (includeShader) {
                    callJson["shader_flags"] = call.shaderDesc.flags.value;
                    callJson["other_mode_high"] = call.shaderDesc.otherMode.H;
                    callJson["other_mode_low"] = call.shaderDesc.otherMode.L;
                }
                projectionJson["calls"].push_back(std::move(callJson));
            }
            pairJson["projections"].push_back(std::move(projectionJson));
        }
        result["pairs"].push_back(std::move(pairJson));
    }
    return result;
}

void writeJson(const std::string &path, const json &value) {
    const std::string bytes = value.dump(2) + '\n';
    writeFile(path, bytes.data(), bytes.size());
}

void run(const Options &options) {
    const auto phase = [](const char *name) {
        std::cerr << "jfg-rt64-headless phase=" << name << '\n' << std::flush;
    };
    auto ram = readWordSwappedRam(options.ram);
    jfg::StaticModel character, hand;
    std::vector<std::pair<uint32_t, uint32_t>> sources;
    if (options.matrixBase) {
        character = jfg::decode_static_character(ram.data(), RdramSize, options.list, options.vertexBase, options.matrixBase);
        sources = character.sources;
        if (options.handList) {
            hand = jfg::decode_static_character(ram.data(), RdramSize, options.handList,
                                                options.handVertices, options.handMatrix, true);
            sources.insert(sources.end(), hand.sources.begin(), hand.sources.end());
        }
    } else {
        const auto decoded = jfg::decode_model(ram.data(), RdramSize, options.list, options.vertexBase);
        sources = {{options.list, decoded.commandCount * 8U},
            {decoded.vertexAddress & 0x7fffffU, decoded.vertexCount * 10U},
            {decoded.polygonAddress & 0x7fffffU, uint32_t(decoded.triangles.size()) * 16U},
            {decoded.textureDmaAddress & 0x7fffffU, 48U},
            {decoded.imageAddress & 0x7fffffU, 4096U}};
    }
    const std::array<std::pair<uint32_t, uint32_t>, 3> reserved = {{
        {ColorAddress, Width * Height * 2U},
        {DepthAddress, Width * Height * 2U},
        {ScratchAddress, 96U}
    }};
    for (const auto &[base, size] : reserved) {
        if (uint64_t(base) + size > RdramSize ||
            !std::all_of(ram.begin() + base, ram.begin() + base + size, [](uint8_t byte) { return byte == 0; })) {
            throw std::runtime_error("Reserved framebuffer/scratch RDRAM is not empty");
        }
        for (const auto &[source, length] : sources) {
            if (overlaps(base, size, source, length)) {
                throw std::runtime_error("Reserved framebuffer/scratch overlaps model source data");
            }
        }
    }
    if ((ColorAddress + Width * Height * 2U > DepthAddress) ||
        (DepthAddress + Width * Height * 2U > ScratchAddress) ||
        (ScratchAddress + 96U > RdramSize)) {
        throw std::runtime_error("Reserved framebuffer/scratch ranges overlap");
    }
    auto graphics = plume::CreateD3D12Interface();
    if (!graphics) throw std::runtime_error("D3D12 interface unavailable");
    auto device = graphics->createDevice();
    if (!device) throw std::runtime_error("D3D12 device unavailable");
    const auto description = device->getDescription();
    if (description.vendor != plume::RenderDeviceVendor::NVIDIA) {
        throw std::runtime_error("Expected NVIDIA D3D12 hardware device; selected device: " + description.name);
    }
    phase("device");
    const auto vendorId = static_cast<uint32_t>(description.vendor);
    std::cout << "D3D12 device: " << description.name << " vendor=0x" << std::hex
              << vendorId << " driver=0x" << description.driverVersion << std::dec << '\n';

    RT64::UserConfiguration userConfig;
    userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
    userConfig.resolution = RT64::UserConfiguration::Resolution::Original;
    userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
    userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
    userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
    userConfig.internalColorFormat = RT64::UserConfiguration::InternalColorFormat::Standard;
    userConfig.hardwareResolve = RT64::UserConfiguration::HardwareResolve::Disabled;
    userConfig.idleWorkActive = false;
    RT64::EmulatorConfiguration emulatorConfig;
    emulatorConfig.framebuffer.renderToRAM = false;
    RT64::EnhancementConfiguration enhancementConfig;
    const auto multisampling = RT64::RasterShader::generateMultisamplingPattern(1, device->getCapabilities().sampleLocations);

    auto shaderLibrary = std::make_unique<RT64::ShaderLibrary>(false, false);
    shaderLibrary->setupCommonShaders(graphics.get(), device.get());
    shaderLibrary->setupMultisamplingShaders(graphics.get(), device.get(), multisampling);
    auto rasterShaders = std::make_unique<RT64::RasterShaderCache>(1, 1);
    rasterShaders->setup(device.get(), graphics->getCapabilities().shaderFormat, shaderLibrary.get(), multisampling);
    // This one-shot proof uses RT64's stock uber pipelines. State::fullSync
    // still submits optional specialized shaders, whose DXIL linker requires
    // an exact compiler version string match not present in the fixed blobs.
    // Stop only that optional worker before the first description is queued.
    for (const auto &compiler : rasterShaders->compilationThreads) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!compiler->threadRunning && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        if (!compiler->threadRunning) {
            throw std::runtime_error("RT64 shader compiler worker did not start");
        }
    }
    rasterShaders->compilationThreads.clear();
    phase("shaders");
    RT64::RenderWorker framebufferWorker(device.get(), "JFG Framebuffer", plume::RenderCommandListType::DIRECT);
    RT64::RenderWorker textureDirectWorker(device.get(), "JFG Texture Direct", plume::RenderCommandListType::DIRECT);
    RT64::RenderWorker textureCopyWorker(device.get(), "JFG Texture Copy", plume::RenderCommandListType::COPY);
    RT64::RenderWorker workloadWorker(device.get(), "JFG Workload", plume::RenderCommandListType::DIRECT);
    RT64::BufferUploader drawDataUploader(device.get()), transformsUploader(device.get()), tilesUploader(device.get());
    RT64::BufferUploader extrasUploader(device.get()), velocityUploader(device.get()), workloadTilesUploader(device.get());
    auto textureCache = std::make_unique<RT64::TextureCache>(&textureDirectWorker, &textureCopyWorker, 1, shaderLibrary.get());
    textureCache->setReplacementPoolMaxSize(options.texturePool);
    RT64::SharedQueueResources shared;
    shared.setUserConfig(userConfig, false);
    shared.setEmulatorConfig(emulatorConfig);
    shared.setEnhancementConfig(enhancementConfig);
    shared.setSwapChainSize(Width, Height); // Target dimensions only; no swapchain exists.
    shared.renderTargetManager.setMultisampling(multisampling);
    shared.renderTargetManager.setUsesHDR(false);
    RT64::PresentQueue inertPresentQueue; // Never setup/start its swapchain thread.
    RT64::WorkloadQueue workloadQueue;
    RT64::WorkloadQueue::External workloadExt;
    workloadExt.device = device.get();
    workloadExt.workloadGraphicsWorker = &workloadWorker;
    workloadExt.workloadExtrasUploader = &extrasUploader;
    workloadExt.workloadVelocityUploader = &velocityUploader;
    workloadExt.workloadTilesUploader = &workloadTilesUploader;
    workloadExt.presentQueue = &inertPresentQueue;
    workloadExt.sharedResources = &shared;
    workloadExt.rasterShaderCache = rasterShaders.get();
    workloadExt.textureCache = textureCache.get();
    workloadExt.shaderLibrary = shaderLibrary.get();
    workloadExt.createdGraphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
    workloadQueue.setup(workloadExt);
    workloadQueue.ubershadersOnly = true;
    phase("caches_queues");

    uint32_t miInterrupt = 0;
    RT64::Interpreter interpreter;
    RT64::State state(ram.data(), &miInterrupt, &checkInterrupts);
    interpreter.setup(&state);
    RT64::State::External stateExt{};
    stateExt.interpreter = &interpreter;
    stateExt.device = device.get();
    stateExt.framebufferGraphicsWorker = &framebufferWorker;
    stateExt.shaderLibrary = shaderLibrary.get();
    stateExt.drawDataUploader = &drawDataUploader;
    stateExt.transformsUploader = &transformsUploader;
    stateExt.tilesUploader = &tilesUploader;
    stateExt.workloadQueue = &workloadQueue;
    stateExt.presentQueue = &inertPresentQueue;
    stateExt.sharedQueueResources = &shared;
    stateExt.rasterShaderCache = rasterShaders.get();
    stateExt.textureCache = textureCache.get();
    stateExt.emulatorConfig = &emulatorConfig;
    stateExt.enhancementConfig = &enhancementConfig;
    stateExt.userConfig = &userConfig;
    stateExt.createdGraphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
    state.setup(stateExt);
    state.rdp->setGBI();
    // Reuse only RT64's F3D state constants. The adapter decodes JFG opcodes.
    auto &f3dStateConstants = interpreter.gbiManager.gbiCache[static_cast<uint32_t>(RT64::GBIUCode::F3D)];
    RT64::GBI_F3D::setup(&f3dStateConstants);
    state.rsp->setGBI(&f3dStateConstants);
    workloadQueue.workloads[workloadQueue.writeCursor].begin(0);
    phase("state");
    state.rdp->setDepthImage(DepthAddress);
    state.rdp->setScissor(0, 0, 0, Width * 4, Height * 4);
    // Explicit RDP clears establish the complete 320x240 extent and far Z.
    // The first fill-only framebuffer pair is RT64's recognized depth clear.
    state.rdp->setOtherMode(G_CYC_FILL, 0);
    state.rdp->setColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, Width, DepthAddress);
    state.rdp->setFillColor(0xfffcfffcU);
    state.rdp->fillRect(0, 0, (Width - 1) * 4, (Height - 1) * 4);
    state.rdp->setColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, Width, ColorAddress);
    state.rdp->setFillColor(0x00010001U); // Opaque black in RGBA16.
    state.rdp->fillRect(0, 0, (Width - 1) * 4, (Height - 1) * 4);
    // The observed two-cycle combiner uses PRIMITIVE. The caller's material
    // color was not captured with the isolated list, so choose opaque white.
    state.rdp->setPrimColor(0, 0, 0xffffffffU);
    state.rdp->setEnvColor(0xffffffffU);
    setupControlledCamera(state, options.matrixBase != 0);
    // The observed blender uses fog color weighted by SHADE_ALPHA. Preserve
    // JFG's G_FOG flag while fixing the isolated proof's fog factor at zero,
    // so its existing FC/EF state selects the combiner result.
    state.rsp->setFog(0, 0);

    auto stats = options.matrixBase ? jfg::draw_static_character(state, character, ScratchAddress) :
        jfg::draw_model(state, options.list, options.vertexBase, ScratchAddress);
    if (options.handList) {
        const auto extra = jfg::draw_static_character(state, hand, ScratchAddress);
        stats.commands += extra.commands;
        stats.vertices += extra.vertices;
        stats.triangles += extra.triangles;
        stats.rdpCommands += extra.rdpCommands;
    }
    phase("clears_draw");
    auto &submittedWorkload = workloadQueue.workloads[workloadQueue.writeCursor];
    const auto &primitive = state.rdp->primColorStack[state.rdp->primColorStackSize - 1];
    const auto &environment = state.rdp->envColorStack[state.rdp->envColorStackSize - 1];
    json cpuDiagnostic = {
        {"status", "before_full_sync"},
        {"draw_call_triangles_pending", state.drawCall.triangleCount},
        {"draw_call_other_mode_high", state.drawCall.otherMode.H},
        {"draw_call_other_mode_low", state.drawCall.otherMode.L},
        {"rdp_other_mode_high", state.rdp->otherMode.H},
        {"rdp_other_mode_low", state.rdp->otherMode.L},
        {"primitive_color", {float(primitive.x), float(primitive.y), float(primitive.z), float(primitive.w)}},
        {"environment_color", {float(environment.x), float(environment.y), float(environment.z), float(environment.w)}},
        {"before", inspectWorkload(submittedWorkload, false)}
    };
    writeJson(options.out + ".cpu.json", cpuDiagnostic);
    state.fullSync();
    phase("full_sync");
    cpuDiagnostic["ubershader_pipelines_ready"] = rasterShaders->shaderUber->pipelinesCreated;
    cpuDiagnostic["ubershader_pipeline_count"] = std::count_if(
        std::begin(rasterShaders->shaderUber->pipelines), std::end(rasterShaders->shaderUber->pipelines),
        [](const auto &pipeline) { return bool(pipeline); });
    workloadQueue.waitForWorkloadId(1);
    workloadQueue.waitForIdle();
    phase("workload_done");

    std::scoped_lock<std::mutex> workloadLock(shared.workloadMutex);
    cpuDiagnostic["after"] = inspectWorkload(submittedWorkload, true);
    cpuDiagnostic["status"] = "workload_complete";
    writeJson(options.out + ".cpu.json", cpuDiagnostic);
    const RT64::RenderTargetKey key(ColorAddress, Width, 2, RT64::Framebuffer::Type::Color);
    phase("target_lookup_begin");
    const auto targetIt = shared.renderTargetManager.targetMap.find(key.hash());
    const bool found = targetIt != shared.renderTargetManager.targetMap.end();
    std::cerr << "jfg-rt64-headless target_found=" << found
              << " target_count=" << shared.renderTargetManager.targetMap.size();
    if (found && targetIt->second) {
        std::cerr << " width=" << targetIt->second->width
                  << " height=" << targetIt->second->height
                  << " format=" << static_cast<uint32_t>(targetIt->second->format)
                  << " has_texture=" << bool(targetIt->second->texture);
    }
    std::cerr << '\n' << std::flush;
    if ((targetIt == shared.renderTargetManager.targetMap.end()) || !targetIt->second ||
        !targetIt->second->texture || targetIt->second->width != Width || targetIt->second->height != Height ||
        targetIt->second->format != plume::RenderFormat::R8G8B8A8_UNORM) {
        phase("target_validation_failed");
        throw std::runtime_error("RT64 did not produce the expected 320x240 SDR color target");
    }
    auto *resolvedTexture = targetIt->second->getResolvedTexture();
    if (!resolvedTexture) {
        phase("target_texture_missing");
        throw std::runtime_error("RT64 color target has no resolved texture");
    }
    phase("target_validated");
    const auto pixels = readColorTarget(device.get(), resolvedTexture);
    phase("readback");
    uint32_t coloredPixels = 0;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        coloredPixels += (pixels[i] | pixels[i + 1] | pixels[i + 2]) != 0;
    }
    writeFile(options.out + ".rgba", pixels.data(), pixels.size());

    std::ofstream meta(options.out + ".json", std::ios::trunc);
    if (!meta) throw std::runtime_error("Cannot create JSON metadata");
    json metadata = {
        {"renderer", "RT64 framebuffer offscreen"},
        {"api", "D3D12"},
        {"shader_mode", "stock_RT64_ubershader_specialization_disabled"},
        {"device", description.name},
        {"vendor", vendorId},
        {"driver_version", description.driverVersion},
        {"width", Width},
        {"height", Height},
        {"format", "R8G8B8A8_UNORM"},
        {"bytes", pixels.size()},
        {"colored_pixels", coloredPixels},
        {"status", coloredPixels ? "candidate_frame_unreviewed" : "failed_no_colored_pixels"},
        {"frame_validated", false},
        {"cpu_diagnostic", options.out + ".cpu.json"},
        {"camera", options.matrixBase ? "controlled_orthographic_character_yaw20" : "controlled_orthographic_model35"},
        {"pose", options.matrixBase ? "neutral_bone_hierarchy_float_matrices" : "static_quad"},
        {"matrix_loads", character.matrixLoads},
        {"texture_loads", character.textureLoads + hand.textureLoads},
        {"body_triangles", character.stats.triangles},
        {"attachment_triangles", hand.stats.triangles},
        {"camera_depth", options.matrixBase ? "negative_Z_consistent_with_face_winding" : "quad_reference"},
        {"viewport_z", "scale_and_translate_511_over_1024"},
        {"background", "controlled_RDP_fill_RGBA16_00010001"},
        {"depth_clear", "controlled_RDP_fill_FFFCFFFC_far"},
        {"primitive_color", "FFFFFFFF_controlled"},
        {"environment_color", "FFFFFFFF_controlled"},
        {"fog", "original_flag_preserved_with_controlled_zero_factor"},
        {"depth_target", true},
        {"depth_mode", "observed_RDP_othermode_forwarded"},
        {"list_address", options.list},
        {"vertex_base", options.vertexBase},
        {"matrix_base", options.matrixBase},
        {"draw_commands", stats.commands},
        {"triangles", stats.triangles}
    };
    meta << metadata.dump(2) << '\n';
    meta.flush();
    if (!meta) throw std::runtime_error("Cannot write JSON metadata");
    if (coloredPixels == 0) throw std::runtime_error("RT64 color target has no colored pixels; raw bytes and diagnostics retained");
    std::cout << "Wrote " << options.out << ".rgba and .json (" << stats.triangles << " triangles)\n";
}
}

int main(int argc, char **argv) {
#ifdef _WIN32
    // Keep abort diagnostics on stderr, without a blocking Windows Error
    // Reporting dialog when an RT64 worker hits a fatal error.
    _set_abort_behavior(0, _CALL_REPORTFAULT);
#endif
    try {
        run(parseOptions(argc, argv));
        return 0;
    }
    catch (const std::exception &e) {
        std::cerr << "jfg-rt64-headless: " << e.what() << '\n';
        return 1;
    }
}
