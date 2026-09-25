// Own native D3D11 backend, with no console hardware or command interpreter.
#include "renderer.h"
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <iostream>
#include <algorithm>

namespace jfg_native {
using Microsoft::WRL::ComPtr;
using Vertex = MeshVertex;
static void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static void check(HRESULT result, const char *what) {
    if (FAILED(result)) throw std::runtime_error(std::string(what) + " HRESULT=" + std::to_string(uint32_t(result)));
}
static const char *Shader = R"(
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
#if JFG_WORLD_CAMERA
cbuffer CameraData : register(b0) { row_major float4x4 worldToClip; };
#endif
struct Input { float3 position : POSITION; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 tint : COLOR0; };
Output vertexMain(Input input) {
    Output output;
    float3 p = input.position;
    // Controlled camera with conventional PC depth range [0,1].
#if JFG_WORLD_CAMERA
    output.position = mul(float4(p, 1.0), worldToClip);
#elif JFG_INTEGRATION_CAMERA
    output.position = float4((-0.939692621 * p.x - 0.342020143 * p.z) / 360.0,
                             (p.y - 102.0) / 270.0,
                             0.5 + (-0.342020143 * p.x + 0.939692621 * p.z) / 2048.0, 1.0);
#elif JFG_CHARACTER_CAMERA
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

static ComPtr<ID3DBlob> compile(const char *entry, const char *target, CameraPreset camera) {
    ComPtr<ID3DBlob> code, errors;
    const D3D_SHADER_MACRO macros[] = {{"JFG_WIDE_CAMERA", camera == CameraPreset::Wide ? "1" : "0"},
                                     {"JFG_CHARACTER_CAMERA", camera == CameraPreset::Character ? "1" : "0"},
                                     {"JFG_INTEGRATION_CAMERA", camera == CameraPreset::Integration ? "1" : "0"},
                                     {"JFG_WORLD_CAMERA", camera == CameraPreset::World ? "1" : "0"}, {nullptr, nullptr}};
    const HRESULT result = D3DCompile(Shader, std::strlen(Shader), "native_preview.hlsl", macros, nullptr,
                                     entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                                     0, &code, &errors);
    if (FAILED(result) && errors) std::cerr.write(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize());
    check(result, "Compile native HLSL"); return code;
}

struct NativeRenderer::State {
    struct Resource {
        std::shared_ptr<const AssetPackage> assets;
        std::vector<ComPtr<ID3D11ShaderResourceView>> textureViews;
        ComPtr<ID3D11Buffer> buffer;
    };
    std::vector<Resource> catalog;
    CameraPreset preset;
    DXGI_ADAPTER_DESC1 adapterInfo{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> cameraBuffer;
    ComPtr<ID3D11Texture2D> color, depth, staging;
    ComPtr<ID3D11RenderTargetView> colorView;
    ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11RasterizerState> raster[2];
    ComPtr<ID3D11DepthStencilState> depthState[2];
    ComPtr<ID3D11BlendState> blendState[2];
    ComPtr<ID3D11SamplerState> samplerState[4];
    std::vector<uint8_t> pixels = std::vector<uint8_t>(Width * Height * 4);
    State(std::vector<std::shared_ptr<const AssetPackage>> packages, CameraPreset camera) : preset(camera) {
        require(!packages.empty() && packages.size() <= 64, "Unbounded native resource catalog");
        for (auto &package : packages) {
            require(bool(package) && !package->vertices.empty(), "Empty renderer resources");
            catalog.push_back({std::move(package), {}, {}});
        }
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        // Select the known hardware vendor. Never fall back to WARP/software.
        ComPtr<IDXGIFactory1> factory;
        check(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.GetAddressOf())), "DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        for (unsigned index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            const HRESULT result = factory->EnumAdapters1(index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            check(result, "Enumerate hardware adapter");
            DXGI_ADAPTER_DESC1 info{}; check(candidate->GetDesc1(&info), "Adapter description");
            if (info.VendorId == 0x10DE && !(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { adapter = candidate; adapterInfo = info; break; }
        }
        require(bool(adapter), "NVIDIA hardware adapter not found");
        D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED,
                               requested, 1, D3D11_SDK_VERSION, &device, &level, &context), "Native D3D11 device");
        for (auto &entry : catalog) {
          entry.textureViews.resize(entry.assets->textures.size());
          for (size_t i = 0; i < entry.assets->textures.size(); ++i) {
            const auto &texture = entry.assets->textures[i];
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = texture.width; desc.Height = texture.height; desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{texture.pixels.data(), texture.width * 4, 0};
            ComPtr<ID3D11Texture2D> resource;
            check(device->CreateTexture2D(&desc, &data, &resource), "Upload native texture");
            check(device->CreateShaderResourceView(resource.Get(), nullptr, &entry.textureViews[i]), "Texture view");
          }
          D3D11_BUFFER_DESC vb{}; vb.ByteWidth = UINT(entry.assets->vertices.size() * sizeof(Vertex));
          vb.Usage = D3D11_USAGE_DYNAMIC; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
          D3D11_SUBRESOURCE_DATA vbData{entry.assets->vertices.data(), 0, 0};
          check(device->CreateBuffer(&vb, &vbData, &entry.buffer), "Vertex buffer");
        }
        const auto vs = compile("vertexMain", "vs_5_0", camera), ps = compile("pixelMain", "ps_5_0", camera);
        check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader), "Vertex shader");
        check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader), "Pixel shader");
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        check(device->CreateInputLayout(elements, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &layout), "Vertex layout");
        if (preset == CameraPreset::World) {
            D3D11_BUFFER_DESC cb{}; cb.ByteWidth = sizeof(Matrix); cb.Usage = D3D11_USAGE_DYNAMIC;
            cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            check(device->CreateBuffer(&cb, nullptr, &cameraBuffer), "Native camera buffer");
        }
        D3D11_TEXTURE2D_DESC target{};
        target.Width = Width; target.Height = Height; target.MipLevels = target.ArraySize = 1;
        target.Format = DXGI_FORMAT_R8G8B8A8_UNORM; target.SampleDesc.Count = 1; target.BindFlags = D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&target, nullptr, &color), "Color target");
        check(device->CreateRenderTargetView(color.Get(), nullptr, &colorView), "Color view");
        target.Format = DXGI_FORMAT_D32_FLOAT; target.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        check(device->CreateTexture2D(&target, nullptr, &depth), "Depth target");
        check(device->CreateDepthStencilView(depth.Get(), nullptr, &depthView), "Depth view");
        target.Format = DXGI_FORMAT_R8G8B8A8_UNORM; target.BindFlags = 0;
        target.Usage = D3D11_USAGE_STAGING; target.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&target, nullptr, &staging), "Readback target");
        for (unsigned i = 0; i < 2; ++i) {
            D3D11_RASTERIZER_DESC desc{}; desc.FillMode = D3D11_FILL_SOLID; desc.CullMode = i ? D3D11_CULL_NONE : D3D11_CULL_BACK;
            desc.FrontCounterClockwise = TRUE; // Winding of the converted source meshes.
            desc.DepthClipEnable = TRUE;
            check(device->CreateRasterizerState(&desc, &raster[i]), "Rasterizer state");
        }
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
        for (unsigned i = 0; i < 4; ++i) {
            D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = i & 1 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressV = i & 2 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD = D3D11_FLOAT32_MAX;
            check(device->CreateSamplerState(&sampler, &samplerState[i]), "Native sampler");
        }
        D3D11_VIEWPORT viewport{0, 0, float(Width), float(Height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(layout.Get()); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader.Get(), nullptr, 0); context->PSSetShader(pixelShader.Get(), nullptr, 0);
    }
    ~State() { if (context) { context->ClearState(); context->Flush(); } }
    const std::vector<uint8_t> &draw(const std::vector<RenderInstance> &instances, const WorldCamera *camera) {
        require(instances.size() <= 64, "Native frame instance budget exceeded");
        require((preset == CameraPreset::World) == (camera != nullptr), "Camera does not match renderer mode");
        const auto cameraMatrix = camera ? camera->matrix() : identityMatrix();
        std::vector<std::vector<Vertex>> posed;
        for (const auto &instance : instances) {
            require(instance.mesh < catalog.size(), "Invalid native mesh handle");
            const auto &assets = catalog[instance.mesh].assets;
            require(instance.bones.size() == assets->skeleton.size(), "Invalid render skeleton");
            for (const auto &matrix : instance.bones) for (float value : matrix)
                require(std::isfinite(value), "Nonfinite render pose");
            for (float value : instance.world) require(std::isfinite(value), "Nonfinite render transform");
            posed.push_back(assets->vertices);
            if (assets->rigged) for (size_t i = 0; i < posed.back().size(); ++i) {
                const auto &v = assets->vertices[i];
                const auto p = transform({v.x, v.y, v.z}, instance.bones[assets->vertexBones[i]]);
                posed.back()[i].x = p[0]; posed.back()[i].y = p[1]; posed.back()[i].z = p[2];
            }
            if (!assets->rigged && instance.world != identityMatrix()) for (auto &v : posed.back()) {
                const auto p = transform({v.x, v.y, v.z}, instance.world); v.x = p[0]; v.y = p[1]; v.z = p[2];
            }
        }
        if (camera) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(context->Map(cameraBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update native camera");
            std::memcpy(mapped.pData, cameraMatrix.data(), sizeof(Matrix)); context->Unmap(cameraBuffer.Get(), 0);
            context->VSSetConstantBuffers(0, 1, cameraBuffer.GetAddressOf());
        }
        const float clear[4] = {camera ? camera->clearColor[0] : 0, camera ? camera->clearColor[1] : 0, camera ? camera->clearColor[2] : 0, 1};
        context->ClearRenderTargetView(colorView.Get(), clear);
        context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        context->OMSetRenderTargets(1, colorView.GetAddressOf(), depthView.Get());
        struct Job { size_t instance, draw; float depth; };
        for (unsigned alpha = 0; alpha < 2; ++alpha) {
          std::vector<Job> jobs;
          for (size_t i = 0; i < posed.size(); ++i) {
            const auto &entry = catalog[instances[i].mesh];
            for (size_t d = 0; d < entry.assets->draws.size(); ++d) {
                const auto &draw = entry.assets->draws[d]; if (bool(draw.flags & 2) != bool(alpha)) continue;
                float depth = 0;
                if (camera && alpha) {
                    Vec3 center{};
                    for (size_t v = draw.first; v < draw.first + draw.count; ++v) {
                        center[0] += posed[i][v].x; center[1] += posed[i][v].y; center[2] += posed[i][v].z;
                    }
                    for (auto &value : center) value /= float(draw.count);
                    depth = dot(subtract(center, camera->eye), normalized(subtract(camera->target, camera->eye)));
                }
                jobs.push_back({i, d, depth});
            }
          }
          if (camera && alpha) std::stable_sort(jobs.begin(), jobs.end(), [](const Job &a, const Job &b) { return a.depth > b.depth; });
          size_t uploaded = SIZE_MAX;
          for (const auto &job : jobs) {
            auto &entry = catalog[instances[job.instance].mesh]; const auto &vertices = posed[job.instance];
            if (uploaded != job.instance) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(context->Map(entry.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Update native mesh");
            std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
            context->Unmap(entry.buffer.Get(), 0);
            UINT stride = sizeof(Vertex), offset = 0; context->IASetVertexBuffers(0, 1, entry.buffer.GetAddressOf(), &stride, &offset);
            uploaded = job.instance;
            }
                const auto &draw = entry.assets->draws[job.draw];
                context->RSSetState(raster[draw.flags & 1].Get()); context->OMSetDepthStencilState(depthState[alpha].Get(), 0);
                context->OMSetBlendState(blendState[alpha].Get(), nullptr, 0xffffffff);
                context->PSSetShaderResources(0, 1, entry.textureViews[draw.texture].GetAddressOf());
                context->PSSetSamplers(0, 1, samplerState[(draw.flags >> 2) & 3].GetAddressOf());
                context->Draw(draw.count, draw.first);
          }
        }
        context->OMSetRenderTargets(0, nullptr, nullptr); context->CopyResource(staging.Get(), color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Wait and read GPU framebuffer");
        for (unsigned y = 0; y < Height; ++y)
            std::memcpy(pixels.data() + y * Width * 4, static_cast<uint8_t *>(mapped.pData) + y * mapped.RowPitch, Width * 4);
        context->Unmap(staging.Get(), 0);
        return pixels;
    }
};
NativeRenderer::NativeRenderer(std::shared_ptr<const AssetPackage> assets, CameraPreset camera)
    : NativeRenderer(std::vector<std::shared_ptr<const AssetPackage>>{std::move(assets)}, camera) {}
NativeRenderer::NativeRenderer(std::vector<std::shared_ptr<const AssetPackage>> catalog, CameraPreset camera)
    : state_(std::make_unique<State>(std::move(catalog), camera)) {}
NativeRenderer::~NativeRenderer() = default;
const std::vector<uint8_t> &NativeRenderer::draw(const std::vector<RenderInstance> &instances) { return state_->draw(instances, nullptr); }
const std::vector<uint8_t> &NativeRenderer::draw(const std::vector<RenderInstance> &instances, const WorldCamera &camera) { return state_->draw(instances, &camera); }
uint32_t NativeRenderer::vendor() const { return state_->adapterInfo.VendorId; }
}
