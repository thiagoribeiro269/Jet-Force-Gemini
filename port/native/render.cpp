// Native Windows mesh preview. No console CPU, command interpreter or emulator.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
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

using Microsoft::WRL::ComPtr;
constexpr unsigned Width = 640, Height = 480;

static void check(HRESULT result, const char *what) {
    if (FAILED(result)) throw std::runtime_error(std::string(what) + " HRESULT=" + std::to_string(uint32_t(result)));
}
static void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }

struct Reader {
    std::vector<uint8_t> bytes;
    size_t offset = 0;
    explicit Reader(const char *path) {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        require(bool(in), "Cannot open private scene");
        const auto size = in.tellg();
        require(size > 24 && size < 64 * 1024 * 1024, "Scene size outside bounds");
        bytes.resize(size_t(size)); in.seekg(0); in.read(reinterpret_cast<char *>(bytes.data()), size);
        require(bool(in), "Scene read failed");
    }
    const uint8_t *take(size_t length) {
        require(offset <= bytes.size() && length <= bytes.size() - offset, "Truncated scene");
        const auto *p = bytes.data() + offset; offset += length; return p;
    }
    uint32_t u32() { uint32_t v; std::memcpy(&v, take(4), 4); return v; }
    float f32() { float v; std::memcpy(&v, take(4), 4); require(std::isfinite(v) && std::abs(v) < 8192, "Invalid scene float"); return v; }
    jfg_native::Vec3 vec3() { return {f32(), f32(), f32()}; }
};
struct Texture { uint32_t id, width, height; std::vector<uint8_t> pixels; ComPtr<ID3D11ShaderResourceView> view; };
struct Draw { uint32_t first, count, texture, flags, model; };
struct Vertex { float x, y, z, u, v, r, g, b, a; };
static_assert(sizeof(Vertex) == 36);

enum class Mode { Neutral, Clip, WideClip, Sequence, Character };
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

static const char *Shader = R"(
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
struct Input { float3 position : POSITION; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
Output vertexMain(Input input) {
    Output output;
    float3 p = input.position;
    // Controlled camera with conventional PC depth range [0,1].
#if JFG_CHARACTER_CAMERA
    output.position = float4((-0.939692621 * p.x - 0.342020143 * p.z) / 280.0,
                             (p.y - 102.0) / 210.0,
                             0.5 + (-0.342020143 * p.x + 0.939692621 * p.z) / 2048.0, 1.0);
#elif JFG_WIDE_CAMERA
    output.position = float4((-0.939692621 * p.x - 0.342020143 * p.z) / 216.0,
                             (p.y - 102.0) / 162.0,
                             0.5 + (-0.342020143 * p.x + 0.939692621 * p.z) / 2048.0, 1.0);
#else
    output.position = float4((-0.939692621 * p.x - 0.342020143 * p.z) / 180.0,
                             (p.y - 113.0) / 135.0,
                             0.5 + (-0.342020143 * p.x + 0.939692621 * p.z) / 2048.0, 1.0);
#endif
    output.uv = input.uv; output.tint = input.tint;
    return output;
}
float4 pixelMain(Output input) : SV_TARGET {
    float4 color = image.Sample(linearSampler, input.uv) * input.tint;
    clip(color.a - 0.01);
    return color;
}
)";

static ComPtr<ID3DBlob> compile(const char *entry, const char *target, bool wide, bool character) {
    ComPtr<ID3DBlob> code, errors;
    const D3D_SHADER_MACRO macros[] = {{"JFG_WIDE_CAMERA", wide ? "1" : "0"},
                                     {"JFG_CHARACTER_CAMERA", character ? "1" : "0"}, {nullptr, nullptr}};
    const HRESULT result = D3DCompile(Shader, std::strlen(Shader), "native_preview.hlsl", macros, nullptr,
                                     entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                     0, &code, &errors);
    if (FAILED(result) && errors) std::cerr.write(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
    check(result, "Compile native HLSL"); return code;
}

static void render(const char *input, const char *prefix, Mode mode, const char *sequencePath) {
    const bool animate = mode != Mode::Neutral;
    const bool wideCamera = mode == Mode::WideClip || mode == Mode::Sequence;
    Reader reader(input);
    const auto *magic = reader.take(8);
    const bool multipleClips = std::memcmp(magic, "JFGNAT3\0", 8) == 0;
    const bool rigged = multipleClips || std::memcmp(magic, "JFGNAT2\0", 8) == 0;
    require(rigged || std::memcmp(magic, "JFGNAT1\0", 8) == 0, "Wrong native scene magic");
    const uint32_t textureCount = reader.u32(), drawCount = reader.u32(), vertexCount = reader.u32();
    const uint32_t boneCount = reader.u32();
    require(boneCount == (rigged ? 21U : 0U) && (!animate || rigged) && textureCount && textureCount <= 64 && drawCount && drawCount <= 1024 &&
            vertexCount && vertexCount <= 65536 && vertexCount % 3 == 0, "Unsupported scene counts");
    std::vector<Texture> textures;
    for (uint32_t t = 0; t < textureCount; ++t) {
        Texture texture;
        texture.id = reader.u32(); texture.width = reader.u32(); texture.height = reader.u32();
        const auto length = reader.u32();
        require(texture.width && texture.width <= 1024 && texture.height && texture.height <= 1024 &&
                uint64_t(texture.width) * texture.height * 4 == length, "Invalid RGBA8 texture");
        const auto *pixels = reader.take(length); texture.pixels.assign(pixels, pixels + length);
        textures.push_back(std::move(texture));
    }
    std::vector<Draw> draws;
    uint32_t coveredVertices = 0;
    for (uint32_t d = 0; d < drawCount; ++d) {
        Draw draw{reader.u32(), reader.u32(), reader.u32(), reader.u32(), reader.u32()};
        require(draw.first == coveredVertices && draw.first <= vertexCount && draw.count && draw.count % 3 == 0 &&
                draw.count <= vertexCount - draw.first && draw.texture < textureCount && draw.flags < 4 &&
                (draw.model == 220 || draw.model == 309), "Invalid native draw range");
        coveredVertices += draw.count; draws.push_back(draw);
    }
    require(coveredVertices == vertexCount, "Mesh coverage incomplete");
    std::vector<Vertex> vertices(vertexCount);
    std::vector<uint32_t> vertexBones(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        std::memcpy(&vertices[i], reader.take(sizeof(Vertex)), sizeof(Vertex));
        if (rigged) { vertexBones[i] = reader.u32(); require(vertexBones[i] < boneCount, "Invalid vertex bone"); }
    }
    std::vector<jfg_native::Bone> skeleton;
    std::vector<jfg_native::Clip> clips;
    if (rigged) {
        for (uint32_t i = 0; i < boneCount; ++i) {
            const auto parent = int32_t(reader.u32());
            require(parent >= -1 && parent < int32_t(i) && (parent == -1) == (i == 0), "Invalid bone hierarchy");
            skeleton.push_back({parent, reader.vec3()});
        }
        const uint32_t clipCount = multipleClips ? reader.u32() : 1;
        require(multipleClips ? (clipCount == 2 || clipCount == 3) : clipCount == 1, "Unsupported clip collection size");
        for (uint32_t i = 0; i < clipCount; ++i) {
            jfg_native::Clip clip;
            clip.id = reader.u32(); const uint32_t keyCount = reader.u32(), looping = reader.u32();
            clip.sourceRate = reader.f32();
            const uint32_t ids[] = {1026, 1030, 1071}, counts[] = {16, 10, 3};
            require(clip.id == ids[i] && keyCount == counts[i] && looping == (i < 2 ? 1U : 0U) && clip.sourceRate == 15,
                    "Unsupported animation profile");
            clip.loop = looping != 0;
            for (uint32_t k = 0; k < keyCount; ++k) {
                jfg_native::Keyframe key; key.root = reader.vec3();
                for (uint32_t b = 0; b < boneCount; ++b) key.angles.push_back(reader.vec3());
                clip.keys.push_back(std::move(key));
            }
            clips.push_back(std::move(clip));
        }
    }
    require(reader.offset == reader.bytes.size(), "Trailing scene payload");
    for (const auto &vertex : vertices) {
        std::array<float, 9> values{}; std::memcpy(values.data(), &vertex, sizeof(vertex));
        for (float value : values) require(std::isfinite(value) && std::abs(value) < 8192, "Invalid vertex value");
    }
    Sequence sequence;
    std::unique_ptr<jfg_native::AnimationPlayer> player;
    jfg_native::CharacterSequence commands;
    std::unique_ptr<jfg_native::CharacterController> character;
    if (mode == Mode::Sequence) {
        require(multipleClips && sequencePath, "Selection sequence requires the multi-clip scene");
        sequence = readSequence(sequencePath, clips);
        player = std::make_unique<jfg_native::AnimationPlayer>(clips, skeleton.size(), clips.front().id);
    }
    if (mode == Mode::Character) {
        require(clips.size() == 3 && sequencePath, "Character commands require the three-clip profile");
        std::ifstream inputCommands(sequencePath, std::ios::ate);
        require(bool(inputCommands) && inputCommands.tellg() < 65536, "Cannot read bounded character commands");
        inputCommands.seekg(0); commands = jfg_native::readCharacterSequence(inputCommands);
        character = std::make_unique<jfg_native::CharacterController>(clips, skeleton.size(), jfg_native::CharacterClips{1071, 1026, 1030});
    }
    // Select the known hardware vendor. Never fall back to WARP/software.
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.GetAddressOf())), "DXGI factory");
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 adapterInfo{};
    for (unsigned index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT result = factory->EnumAdapters1(index, &candidate);
        if (result == DXGI_ERROR_NOT_FOUND) break;
        check(result, "Enumerate hardware adapter");
        DXGI_ADAPTER_DESC1 info{}; check(candidate->GetDesc1(&info), "Adapter description");
        if (info.VendorId == 0x10DE && !(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { adapter = candidate; adapterInfo = info; break; }
    }
    require(bool(adapter), "NVIDIA hardware adapter not found");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
    check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED,
                           requested, 1, D3D11_SDK_VERSION, &device, &level, &context), "Native D3D11 device");
    for (auto &texture : textures) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = texture.width; desc.Height = texture.height; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{texture.pixels.data(), texture.width * 4, 0};
        ComPtr<ID3D11Texture2D> resource;
        check(device->CreateTexture2D(&desc, &data, &resource), "Upload native texture");
        check(device->CreateShaderResourceView(resource.Get(), nullptr, &texture.view), "Texture view");
    }
    const auto vs = compile("vertexMain", "vs_5_0", wideCamera, bool(character)), ps = compile("pixelMain", "ps_5_0", wideCamera, bool(character));
    ComPtr<ID3D11VertexShader> vertexShader; ComPtr<ID3D11PixelShader> pixelShader;
    check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader), "Vertex shader");
    check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader), "Pixel shader");
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ComPtr<ID3D11InputLayout> layout;
    check(device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &layout), "Vertex layout");
    D3D11_BUFFER_DESC vb{}; vb.ByteWidth = UINT(vertices.size() * sizeof(Vertex));
    vb.Usage = D3D11_USAGE_DYNAMIC; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA vbData{vertices.data(), 0, 0}; ComPtr<ID3D11Buffer> buffer;
    check(device->CreateBuffer(&vb, &vbData, &buffer), "Vertex buffer");
    D3D11_TEXTURE2D_DESC target{};
    target.Width = Width; target.Height = Height; target.MipLevels = target.ArraySize = 1;
    target.Format = DXGI_FORMAT_R8G8B8A8_UNORM; target.SampleDesc.Count = 1; target.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color, depth, staging; ComPtr<ID3D11RenderTargetView> colorView; ComPtr<ID3D11DepthStencilView> depthView;
    check(device->CreateTexture2D(&target, nullptr, &color), "Color target");
    check(device->CreateRenderTargetView(color.Get(), nullptr, &colorView), "Color view");
    target.Format = DXGI_FORMAT_D32_FLOAT; target.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    check(device->CreateTexture2D(&target, nullptr, &depth), "Depth target");
    check(device->CreateDepthStencilView(depth.Get(), nullptr, &depthView), "Depth view");
    target.Format = DXGI_FORMAT_R8G8B8A8_UNORM; target.BindFlags = 0;
    target.Usage = D3D11_USAGE_STAGING; target.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&target, nullptr, &staging), "Readback target");
    ComPtr<ID3D11RasterizerState> raster[2];
    for (unsigned i = 0; i < 2; ++i) {
        D3D11_RASTERIZER_DESC desc{}; desc.FillMode = D3D11_FILL_SOLID; desc.CullMode = i ? D3D11_CULL_NONE : D3D11_CULL_BACK;
        desc.FrontCounterClockwise = TRUE; // Winding of the converted source meshes.
        desc.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&desc, &raster[i]), "Rasterizer state");
    }
    ComPtr<ID3D11DepthStencilState> depthState[2]; ComPtr<ID3D11BlendState> blendState[2];
    for (unsigned i = 0; i < 2; ++i) {
        D3D11_DEPTH_STENCIL_DESC desc{}; desc.DepthEnable = TRUE; desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        desc.DepthWriteMask = i ? D3D11_DEPTH_WRITE_MASK_ZERO : D3D11_DEPTH_WRITE_MASK_ALL;
        check(device->CreateDepthStencilState(&desc, &depthState[i]), "Depth state");
        D3D11_BLEND_DESC blend{}; auto &b = blend.RenderTarget[0]; b.BlendEnable = i;
        b.SrcBlend = D3D11_BLEND_SRC_ALPHA; b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; b.BlendOp = D3D11_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D11_BLEND_ONE; b.DestBlendAlpha = D3D11_BLEND_ZERO; b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&blend, &blendState[i]), "Material blend state");
    }
    D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> samplerState; check(device->CreateSamplerState(&sampler, &samplerState), "Native sampler");
    D3D11_VIEWPORT viewport{0, 0, float(Width), float(Height), 0, 1};
    const float black[4] = {0, 0, 0, 1};
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(layout.Get()); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vertex), offset = 0; context->IASetVertexBuffers(0, 1, buffer.GetAddressOf(), &stride, &offset);
    context->VSSetShader(vertexShader.Get(), nullptr, 0); context->PSSetShader(pixelShader.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, samplerState.GetAddressOf());
    std::vector<uint8_t> pixels(Width * Height * 4);
    std::ofstream raw(std::string(prefix) + ".rgba", std::ios::binary);
    require(bool(raw), "Cannot create frame stream");
    const uint32_t frameCount = character ? commands.frames : player ? sequence.frames : animate ? uint32_t(clips.front().keys.size() * 2 + 1) : 1;
    std::vector<std::string> requestTrace, stateTrace;
    size_t eventIndex = 0;
    uint32_t accepted = 0;
    float maximumJump = 0;
    uint32_t colored = 0, minColored = Width * Height, maxColored = 0;
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        if (rigged) {
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
            auto matrices = character ? jfg_native::composePose(skeleton, character->animation().current()) :
                player ? jfg_native::composePose(skeleton, player->current()) :
                jfg_native::pose(skeleton, animate ? &clips.front() : nullptr, float(frame) * 0.5f);
            if (character) for (auto &matrix : matrices) matrix = jfg_native::multiply(matrix, character->world());
            auto posed = vertices;
            for (uint32_t i = 0; i < vertexCount; ++i) {
                const auto p = jfg_native::transform({vertices[i].x, vertices[i].y, vertices[i].z}, matrices[vertexBones[i]]);
                posed[i].x = p[0]; posed[i].y = p[1]; posed[i].z = p[2];
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update native animated mesh");
            std::memcpy(mapped.pData, posed.data(), posed.size() * sizeof(Vertex));
            context->Unmap(buffer.Get(), 0);
        }
        context->ClearRenderTargetView(colorView.Get(), black); context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        context->OMSetRenderTargets(1, colorView.GetAddressOf(), depthView.Get());
        for (unsigned alpha = 0; alpha < 2; ++alpha) for (const auto &draw : draws) {
            if (bool(draw.flags & 2) != bool(alpha)) continue;
            context->RSSetState(raster[draw.flags & 1].Get()); context->OMSetDepthStencilState(depthState[alpha].Get(), 0);
            context->OMSetBlendState(blendState[alpha].Get(), nullptr, 0xffffffff);
            context->PSSetShaderResources(0, 1, textures[draw.texture].view.GetAddressOf());
            context->Draw(draw.count, draw.first);
        }
        context->OMSetRenderTargets(0, nullptr, nullptr); context->CopyResource(staging.Get(), color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Wait and read GPU framebuffer");
        for (unsigned y = 0; y < Height; ++y) std::memcpy(pixels.data() + y * Width * 4, static_cast<uint8_t *>(mapped.pData) + y * mapped.RowPitch, Width * 4);
        context->Unmap(staging.Get(), 0);
        colored = 0;
        for (size_t p = 0; p < pixels.size(); p += 4) colored += bool(pixels[p] | pixels[p + 1] | pixels[p + 2]);
        require(colored > 1000, "Empty native framebuffer");
        minColored = std::min(minColored, colored); maxColored = std::max(maxColored, colored);
        raw.write(reinterpret_cast<const char *>(pixels.data()), pixels.size()); require(bool(raw), "Cannot write readback");
        if (player) player->advance(1.0 / 30);
        if (character) character->advance(1.0 / 30);
    }
    context->ClearState(); context->Flush();
    if (player || character) {
        std::ofstream trace(std::string(prefix) + ".controller.json");
        require(bool(trace), "Cannot create controller trace");
        trace << "{\"requests\":[";
        for (size_t i = 0; i < requestTrace.size(); ++i) trace << (i ? "," : "") << requestTrace[i];
        trace << "],\"states\":[";
        for (size_t i = 0; i < stateTrace.size(); ++i) trace << (i ? "," : "") << stateTrace[i];
        trace << "]}\n"; require(bool(trace), "Cannot write controller trace");
    }
    std::ofstream report(std::string(prefix) + ".json");
    report << "{\"status\":\"rendered_unreviewed\",\"api\":\"D3D11\",\"vendor\":" << adapterInfo.VendorId
           << ",\"width\":" << Width << ",\"height\":" << Height << ",\"triangles\":" << vertexCount / 3
           << ",\"draws\":" << drawCount << ",\"textures\":" << textureCount << ",\"colored_pixels\":" << colored
           << ",\"frame_count\":" << frameCount << ",\"animation_id\":" << (character ? 1071 : animate ? clips.front().id : 0)
           << ",\"source_keyframes\":" << (character ? 3 : clips.empty() ? 0 : clips.front().keys.size()) << ",\"output_fps\":30,\"source_step\":0.5"
           << ",\"sequence\":" << (player ? "true" : "false") << ",\"clip_count\":" << clips.size()
           << ",\"character_commands\":" << (character ? "true" : "false")
           << ",\"selection_requests\":" << requestTrace.size() << ",\"accepted_switches\":" << accepted
           << ",\"instant_pose_max_matrix_delta\":" << maximumJump
           << ",\"camera\":\"" << (character ? "character_path" : wideCamera ? "transition_wide" : "original_native_proof") << "\""
           << ",\"min_colored_pixels\":" << minColored << ",\"max_colored_pixels\":" << maxColored
           << ",\"emulator_dependencies\":false,\"display_list_interpreter\":false,\"animated\":" << (animate ? "true" : "false") << "}\n";
    require(bool(report), "Cannot write report");
    std::cout << "Native D3D11: " << frameCount << " frames, " << vertexCount / 3 << " triangles each\n";
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        Mode mode = Mode::Neutral; const char *sequence = nullptr;
        if (argc == 4 && std::string(argv[3]) == "--animate") mode = Mode::Clip;
        else if (argc == 4 && std::string(argv[3]) == "--animate-wide") mode = Mode::WideClip;
        else if (argc == 5 && std::string(argv[3]) == "--sequence") { mode = Mode::Sequence; sequence = argv[4]; }
        else if (argc == 5 && std::string(argv[3]) == "--character") { mode = Mode::Character; sequence = argv[4]; }
        else require(argc == 3, "usage: jfg_native_preview SCENE OUTPUT_PREFIX [--animate | --animate-wide | --sequence FILE | --character FILE]");
        render(argv[1], argv[2], mode, sequence); return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
