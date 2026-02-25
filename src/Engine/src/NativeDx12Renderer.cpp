#include "Engine/NativeDx12Renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#if defined(_WIN32)
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <objbase.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace engine {
#if defined(_WIN32)
namespace {
constexpr UINT kFrameCount = 2;
constexpr DXGI_FORMAT kBackbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr UINT kSrvDescriptorCount = 64;

struct WireVertex {
    float position[4];
    float color[4];
};

struct TexturedVertex {
    float position[4];
    float uv[2];
    float alpha;
    float padding;
};

struct DecodedImageData {
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
};

struct ClipVertex {
    float x;
    float y;
    float z;
    bool valid;
};

ClipVertex ProjectToNdc(const glm::vec3& point, const glm::mat4& mvp) {
    const glm::vec4 clip = mvp * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0001f) {
        return {0.0f, 0.0f, 0.0f, false};
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {ndc.x, ndc.y, ndc.z, true};
}

void AddLine(std::vector<WireVertex>& vertices, const ClipVertex& a, const ClipVertex& b) {
    if (!a.valid || !b.valid) {
        return;
    }

    constexpr float r = 0.69f;
    constexpr float g = 0.82f;
    constexpr float bl = 1.0f;
    constexpr float alpha = 1.0f;

    vertices.push_back({{a.x, a.y, 0.0f, 1.0f}, {r, g, bl, alpha}});
    vertices.push_back({{b.x, b.y, 0.0f, 1.0f}, {r, g, bl, alpha}});
}

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) {
        return {};
    }

    const int requiredSize = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
    if (requiredSize <= 1) {
        return {};
    }

    std::wstring output(static_cast<std::size_t>(requiredSize - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, output.data(), requiredSize);
    return output;
}

bool DecodeImageWithWic(const std::string& path, DecodedImageData& outImageData, std::string& outError) {
    outImageData = {};
    const std::wstring widePath = Utf8ToWide(path);
    if (widePath.empty()) {
        outError = "Failed to convert texture path to wide string.";
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result) || !factory) {
        outError = "CoCreateInstance(CLSID_WICImagingFactory) failed.";
        return false;
    }

    IWICBitmapDecoder* decoder = nullptr;
    result = factory->CreateDecoderFromFilename(
        widePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(result) || !decoder) {
        if (decoder) {
            decoder->Release();
        }
        factory->Release();
        outError = "WIC CreateDecoderFromFilename failed.";
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result) || !frame) {
        if (frame) {
            frame->Release();
        }
        decoder->Release();
        factory->Release();
        outError = "WIC GetFrame failed.";
        return false;
    }

    IWICFormatConverter* converter = nullptr;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result) || !converter) {
        if (converter) {
            converter->Release();
        }
        frame->Release();
        decoder->Release();
        factory->Release();
        outError = "WIC CreateFormatConverter failed.";
        return false;
    }

    result = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        outError = "WIC converter Initialize failed.";
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    result = converter->GetSize(&width, &height);
    if (FAILED(result) || width == 0 || height == 0) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        outError = "WIC GetSize failed.";
        return false;
    }

    const UINT stride = width * 4;
    outImageData.pixels.resize(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
    result = converter->CopyPixels(nullptr, stride, static_cast<UINT>(outImageData.pixels.size()), outImageData.pixels.data());
    if (FAILED(result)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        outError = "WIC CopyPixels failed.";
        return false;
    }

    outImageData.width = width;
    outImageData.height = height;

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return true;
}

std::string ToErrorMessage(const char* label, HRESULT result) {
    return std::string(label) + " (HRESULT=0x" +
        [&]() {
            char buffer[16] = {};
            snprintf(buffer, sizeof(buffer), "%08X", static_cast<unsigned int>(result));
            return std::string(buffer);
        }() + ")";
}
}

class NativeDx12Renderer::Impl {
public:
    struct FrameContext {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
        UINT64 fenceValue = 0;
    };

    struct CachedModelTexture {
        std::string path;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuDescriptor{};
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuDescriptor{};
        bool hasTransparency = false;
    };

    SDL_Window* window = nullptr;
    HWND hwnd = nullptr;

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> wireRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> wirePipelineState;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> texturedRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> texturedOpaquePipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> texturedTransparentPipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> wireVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> texturedVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;

    std::array<FrameContext, kFrameCount> frames;
    UINT frameIndex = 0;
    UINT rtvDescriptorSize = 0;
    UINT dsvDescriptorSize = 0;
    UINT srvDescriptorSize = 0;
    UINT wireVertexCapacity = 0;
    UINT texturedVertexCapacity = 0;
    D3D12_VERTEX_BUFFER_VIEW wireVertexBufferView{};
    D3D12_VERTEX_BUFFER_VIEW texturedVertexBufferView{};
    UINT64 nextFenceValue = 1;
    UINT srvNextFreeIndex = 1;
    std::vector<UINT> srvFreeList;
    std::vector<std::string> modelTexturePaths;
    std::vector<CachedModelTexture> modelTextures;
    std::unordered_map<UINT64, std::string> debugObjectNames;
    bool comInitialized = false;

    D3D12_CPU_DESCRIPTOR_HANDLE fontSrvCpuDescriptor{};
    D3D12_GPU_DESCRIPTOR_HANDLE fontSrvGpuDescriptor{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};

    void TrackDebugObject(ID3D12Object* object, const std::string& name) {
        if (!object) {
            return;
        }

        const std::wstring wideName = Utf8ToWide(name);
        if (!wideName.empty()) {
            object->SetName(wideName.c_str());
        }

        debugObjectNames[reinterpret_cast<UINT64>(object)] = name;
    }

    std::string DescribeTrackedObjectFromMessage(const char* messageText) const {
        if (!messageText) {
            return {};
        }

        const char* cursor = std::strstr(messageText, "0x");
        while (cursor) {
            char* end = nullptr;
            const unsigned long long objectAddress = std::strtoull(cursor + 2, &end, 16);
            if (end && end != cursor + 2) {
                const auto found = debugObjectNames.find(static_cast<UINT64>(objectAddress));
                if (found != debugObjectNames.end()) {
                    return " [TrackedObject=" + found->second + "]";
                }
            }

            cursor = std::strstr(cursor + 2, "0x");
        }

        return " [TrackedObject=unknown-or-external]";
    }

    bool CreateWirePipeline(std::string& outError) {
        const char* vertexShaderSource = R"(
            struct VSInput {
                float4 position : POSITION;
                float4 color : COLOR;
            };

            struct VSOutput {
                float4 position : SV_POSITION;
                float4 color : COLOR;
            };

            VSOutput main(VSInput input) {
                VSOutput output;
                output.position = input.position;
                output.color = input.color;
                return output;
            }
        )";

        const char* pixelShaderSource = R"(
            struct PSInput {
                float4 position : SV_POSITION;
                float4 color : COLOR;
            };

            float4 main(PSInput input) : SV_TARGET {
                return input.color;
            }
        )";

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> compileErrors;

        HRESULT result = D3DCompile(
            vertexShaderSource,
            std::strlen(vertexShaderSource),
            "NativeDx12WireVS",
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            &vertexShader,
            &compileErrors);
        if (FAILED(result)) {
            outError = "Failed to compile native DX12 wireframe vertex shader.";
            if (compileErrors && compileErrors->GetBufferPointer()) {
                outError += " ";
                outError += static_cast<const char*>(compileErrors->GetBufferPointer());
            }
            return false;
        }

        compileErrors.Reset();
        result = D3DCompile(
            pixelShaderSource,
            std::strlen(pixelShaderSource),
            "NativeDx12WirePS",
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            &pixelShader,
            &compileErrors);
        if (FAILED(result)) {
            outError = "Failed to compile native DX12 wireframe pixel shader.";
            if (compileErrors && compileErrors->GetBufferPointer()) {
                outError += " ";
                outError += static_cast<const char*>(compileErrors->GetBufferPointer());
            }
            return false;
        }

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature;
        compileErrors.Reset();
        result = D3D12SerializeRootSignature(
            &rootSignatureDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSignature,
            &compileErrors);
        if (FAILED(result)) {
            outError = "Failed to serialize native DX12 wireframe root signature.";
            if (compileErrors && compileErrors->GetBufferPointer()) {
                outError += " ";
                outError += static_cast<const char*>(compileErrors->GetBufferPointer());
            }
            return false;
        }

        result = device->CreateRootSignature(
            0,
            serializedRootSignature->GetBufferPointer(),
            serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS(&wireRootSignature));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateRootSignature failed", result);
            return false;
        }
        TrackDebugObject(wireRootSignature.Get(), "WireRootSignature");

        const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = wireRootSignature.Get();
        psoDesc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        psoDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};

        D3D12_BLEND_DESC blendDesc{};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
            TRUE, FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL
        };
        for (auto& target : blendDesc.RenderTarget) {
            target = defaultRenderTargetBlendDesc;
        }
        psoDesc.BlendState = blendDesc;

        psoDesc.SampleMask = UINT_MAX;

        D3D12_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
        rasterizerDesc.FrontCounterClockwise = FALSE;
        rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.MultisampleEnable = FALSE;
        rasterizerDesc.AntialiasedLineEnable = TRUE;
        rasterizerDesc.ForcedSampleCount = 0;
        rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        psoDesc.RasterizerState = rasterizerDesc;

        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        depthStencilDesc.StencilEnable = FALSE;
        depthStencilDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        depthStencilDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        depthStencilDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        depthStencilDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        depthStencilDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        depthStencilDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        depthStencilDesc.BackFace = depthStencilDesc.FrontFace;
        psoDesc.DepthStencilState = depthStencilDesc;

        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.InputLayout = {inputLayout, static_cast<UINT>(_countof(inputLayout))};
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = kBackbufferFormat;
        psoDesc.DSVFormat = kDepthFormat;
        psoDesc.SampleDesc.Count = 1;

        result = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&wirePipelineState));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateGraphicsPipelineState failed", result);
            return false;
        }
        TrackDebugObject(wirePipelineState.Get(), "WirePipelineState");

        return true;
    }

    bool CreateTexturedPipeline(std::string& outError) {
        const char* vertexShaderSource = R"(
            struct VSInput {
                float4 position : POSITION;
                float2 uv : TEXCOORD0;
                float alpha : COLOR0;
                float cutoff : TEXCOORD1;
            };

            struct VSOutput {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
                float alpha : COLOR0;
                float cutoff : TEXCOORD1;
            };

            VSOutput main(VSInput input) {
                VSOutput output;
                output.position = input.position;
                output.uv = input.uv;
                output.alpha = input.alpha;
                output.cutoff = input.cutoff;
                return output;
            }
        )";

        const char* pixelShaderSource = R"(
            Texture2D modelTexture : register(t0);
            Texture2D opacityTexture : register(t1);
            SamplerState linearSampler : register(s0);

            struct PSInput {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
                float alpha : COLOR0;
                float cutoff : TEXCOORD1;
            };

            float4 main(PSInput input) : SV_TARGET {
                float4 color = modelTexture.Sample(linearSampler, input.uv);
                float opacitySample = opacityTexture.Sample(linearSampler, input.uv).r;
                if (input.cutoff < 0.0f) {
                    opacitySample = 1.0f - opacitySample;
                }
                const float opacityScale = saturate(abs(input.alpha));
                const float finalAlpha = opacityScale * saturate(opacitySample);
                if (input.alpha < 0.0f && finalAlpha < saturate(abs(input.cutoff))) {
                    discard;
                }
                color.a *= finalAlpha;
                return color;
            }
        )";

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        Microsoft::WRL::ComPtr<ID3DBlob> compileErrors;

        HRESULT result = D3DCompile(
            vertexShaderSource,
            std::strlen(vertexShaderSource),
            "NativeDx12TexturedVS",
            nullptr,
            nullptr,
            "main",
            "vs_5_0",
            0,
            0,
            &vertexShader,
            &compileErrors);
        if (FAILED(result)) {
            outError = "Failed to compile native DX12 textured vertex shader.";
            if (compileErrors && compileErrors->GetBufferPointer()) {
                outError += " ";
                outError += static_cast<const char*>(compileErrors->GetBufferPointer());
            }
            return false;
        }

        compileErrors.Reset();
        result = D3DCompile(
            pixelShaderSource,
            std::strlen(pixelShaderSource),
            "NativeDx12TexturedPS",
            nullptr,
            nullptr,
            "main",
            "ps_5_0",
            0,
            0,
            &pixelShader,
            &compileErrors);
        if (FAILED(result)) {
            outError = "Failed to compile native DX12 textured pixel shader.";
            if (compileErrors && compileErrors->GetBufferPointer()) {
                outError += " ";
                outError += static_cast<const char*>(compileErrors->GetBufferPointer());
            }
            return false;
        }

        D3D12_DESCRIPTOR_RANGE srvRanges[2]{};
        srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[0].NumDescriptors = 1;
        srvRanges[0].BaseShaderRegister = 0;
        srvRanges[0].RegisterSpace = 0;
        srvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRanges[1].NumDescriptors = 1;
        srvRanges[1].BaseShaderRegister = 1;
        srvRanges[1].RegisterSpace = 0;
        srvRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParameters[2]{};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[0].DescriptorTable.pDescriptorRanges = &srvRanges[0];
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRanges[1];
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.ShaderRegister = 0;
        samplerDesc.RegisterSpace = 0;
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.NumParameters = static_cast<UINT>(_countof(rootParameters));
        rootSignatureDesc.pParameters = rootParameters;
        rootSignatureDesc.NumStaticSamplers = 1;
        rootSignatureDesc.pStaticSamplers = &samplerDesc;
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature;
        compileErrors.Reset();
        result = D3D12SerializeRootSignature(
            &rootSignatureDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSignature,
            &compileErrors);
        if (FAILED(result)) {
            outError = "Failed to serialize native DX12 textured root signature.";
            if (compileErrors && compileErrors->GetBufferPointer()) {
                outError += " ";
                outError += static_cast<const char*>(compileErrors->GetBufferPointer());
            }
            return false;
        }

        result = device->CreateRootSignature(
            0,
            serializedRootSignature->GetBufferPointer(),
            serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS(&texturedRootSignature));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateRootSignature (textured) failed", result);
            return false;
        }
        TrackDebugObject(texturedRootSignature.Get(), "TexturedRootSignature");

        const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = texturedRootSignature.Get();
        psoDesc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        psoDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};

        D3D12_BLEND_DESC blendDesc{};
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC targetBlend{};
        targetBlend.BlendEnable = FALSE;
        targetBlend.LogicOpEnable = FALSE;
        targetBlend.SrcBlend = D3D12_BLEND_ONE;
        targetBlend.DestBlend = D3D12_BLEND_ZERO;
        targetBlend.BlendOp = D3D12_BLEND_OP_ADD;
        targetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        targetBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        targetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        targetBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
        targetBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        for (auto& target : blendDesc.RenderTarget) {
            target = targetBlend;
        }
        psoDesc.BlendState = blendDesc;
        psoDesc.SampleMask = UINT_MAX;

        D3D12_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
        rasterizerDesc.FrontCounterClockwise = FALSE;
        rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.MultisampleEnable = FALSE;
        rasterizerDesc.AntialiasedLineEnable = FALSE;
        rasterizerDesc.ForcedSampleCount = 0;
        rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        psoDesc.RasterizerState = rasterizerDesc;

        D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
        depthStencilDesc.DepthEnable = TRUE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        depthStencilDesc.StencilEnable = FALSE;
        psoDesc.DepthStencilState = depthStencilDesc;

        psoDesc.InputLayout = {inputLayout, static_cast<UINT>(_countof(inputLayout))};
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = kBackbufferFormat;
        psoDesc.DSVFormat = kDepthFormat;
        psoDesc.SampleDesc.Count = 1;

        result = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&texturedOpaquePipelineState));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateGraphicsPipelineState (textured opaque) failed", result);
            return false;
        }
        TrackDebugObject(texturedOpaquePipelineState.Get(), "TexturedOpaquePipelineState");

        targetBlend.BlendEnable = TRUE;
        targetBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        targetBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        targetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        targetBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        for (auto& target : blendDesc.RenderTarget) {
            target = targetBlend;
        }
        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        result = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&texturedTransparentPipelineState));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateGraphicsPipelineState (textured transparent) failed", result);
            return false;
        }
        TrackDebugObject(texturedTransparentPipelineState.Get(), "TexturedTransparentPipelineState");

        return true;
    }

    bool EnsureWireVertexBuffer(UINT requiredVertexCount, std::string& outError) {
        if (requiredVertexCount == 0) {
            return true;
        }

        if (wireVertexBuffer && wireVertexCapacity >= requiredVertexCount) {
            return true;
        }

        const UINT newCapacity = std::max(requiredVertexCount, wireVertexCapacity + 8192U);
        const UINT64 bufferSizeBytes = static_cast<UINT64>(newCapacity) * sizeof(WireVertex);

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = bufferSizeBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> newBuffer;
        const HRESULT result = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&newBuffer));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateCommittedResource for wire vertex buffer failed", result);
            return false;
        }

        wireVertexBuffer = std::move(newBuffer);
        TrackDebugObject(wireVertexBuffer.Get(), "WireVertexBuffer");
        wireVertexCapacity = newCapacity;
        wireVertexBufferView.BufferLocation = wireVertexBuffer->GetGPUVirtualAddress();
        wireVertexBufferView.StrideInBytes = sizeof(WireVertex);
        wireVertexBufferView.SizeInBytes = static_cast<UINT>(bufferSizeBytes);
        return true;
    }

    bool EnsureTexturedVertexBuffer(UINT requiredVertexCount, std::string& outError) {
        if (requiredVertexCount == 0) {
            return true;
        }

        if (texturedVertexBuffer && texturedVertexCapacity >= requiredVertexCount) {
            return true;
        }

        const UINT newCapacity = std::max(requiredVertexCount, texturedVertexCapacity + 8192U);
        const UINT64 bufferSizeBytes = static_cast<UINT64>(newCapacity) * sizeof(TexturedVertex);

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = bufferSizeBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> newBuffer;
        const HRESULT result = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&newBuffer));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateCommittedResource for textured vertex buffer failed", result);
            return false;
        }

        texturedVertexBuffer = std::move(newBuffer);
        TrackDebugObject(texturedVertexBuffer.Get(), "TexturedVertexBuffer");
        texturedVertexCapacity = newCapacity;
        texturedVertexBufferView.BufferLocation = texturedVertexBuffer->GetGPUVirtualAddress();
        texturedVertexBufferView.StrideInBytes = sizeof(TexturedVertex);
        texturedVertexBufferView.SizeInBytes = static_cast<UINT>(bufferSizeBytes);
        return true;
    }

    void ReleaseModelTextures() {
        for (CachedModelTexture& texture : modelTextures) {
            texture.uploadResource.Reset();
            texture.resource.Reset();
            if (texture.srvCpuDescriptor.ptr != 0 || texture.srvGpuDescriptor.ptr != 0) {
                FreeSrvDescriptor(texture.srvCpuDescriptor, texture.srvGpuDescriptor);
            }
            texture.srvCpuDescriptor = {};
            texture.srvGpuDescriptor = {};
            texture.path.clear();
        }
        modelTextures.clear();
        modelTexturePaths.clear();
    }

    bool EnsureModelTexturesUploaded(const ModelData& model, std::string& outError) {
        if (!model.IsValid()) {
            ReleaseModelTextures();
            return false;
        }

        if (model.texturePaths.empty() || model.texCoords.size() != model.positions.size()) {
            ReleaseModelTextures();
            return false;
        }

        if (modelTexturePaths == model.texturePaths && modelTextures.size() == model.texturePaths.size()) {
            return true;
        }

        WaitForGpuIdle();
        ReleaseModelTextures();

        modelTextures.reserve(model.texturePaths.size());
        modelTexturePaths.reserve(model.texturePaths.size());

        for (const std::string& texturePath : model.texturePaths) {
            DecodedImageData decodedImage;
            if (!DecodeImageWithWic(texturePath, decodedImage, outError)) {
                return false;
            }

            modelTexturePaths.push_back(texturePath);
            modelTextures.emplace_back();
            CachedModelTexture& cachedTexture = modelTextures.back();
            cachedTexture.path = texturePath;
            cachedTexture.hasTransparency = false;
            for (std::size_t pixelOffset = 3; pixelOffset < decodedImage.pixels.size(); pixelOffset += 4) {
                if (decodedImage.pixels[pixelOffset] < 250) {
                    cachedTexture.hasTransparency = true;
                    break;
                }
            }

            D3D12_RESOURCE_DESC textureDesc{};
            textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            textureDesc.Alignment = 0;
            textureDesc.Width = decodedImage.width;
            textureDesc.Height = decodedImage.height;
            textureDesc.DepthOrArraySize = 1;
            textureDesc.MipLevels = 1;
            textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            D3D12_HEAP_PROPERTIES textureHeapProps{};
            textureHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            HRESULT result = device->CreateCommittedResource(
                &textureHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&cachedTexture.resource));
            if (FAILED(result)) {
                outError = ToErrorMessage("CreateCommittedResource for model texture failed", result);
                return false;
            }
            TrackDebugObject(cachedTexture.resource.Get(), "ModelTextureResource:" + texturePath);

            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            UINT numRows = 0;
            UINT64 rowSizeInBytes = 0;
            UINT64 uploadBufferSize = 0;
            device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &uploadBufferSize);

            D3D12_HEAP_PROPERTIES uploadHeapProps{};
            uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC uploadBufferDesc{};
            uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadBufferDesc.Alignment = 0;
            uploadBufferDesc.Width = uploadBufferSize;
            uploadBufferDesc.Height = 1;
            uploadBufferDesc.DepthOrArraySize = 1;
            uploadBufferDesc.MipLevels = 1;
            uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
            uploadBufferDesc.SampleDesc.Count = 1;
            uploadBufferDesc.SampleDesc.Quality = 0;
            uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            uploadBufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

            result = device->CreateCommittedResource(
                &uploadHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &uploadBufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&cachedTexture.uploadResource));
            if (FAILED(result)) {
                outError = ToErrorMessage("CreateCommittedResource for model texture upload buffer failed", result);
                return false;
            }
            TrackDebugObject(cachedTexture.uploadResource.Get(), "ModelTextureUploadResource:" + texturePath);

            std::uint8_t* mappedData = nullptr;
            D3D12_RANGE readRange{0, 0};
            result = cachedTexture.uploadResource->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
            if (FAILED(result) || !mappedData) {
                outError = ToErrorMessage("Map for model texture upload buffer failed", result);
                return false;
            }

            const std::size_t sourceRowPitch = static_cast<std::size_t>(decodedImage.width) * 4;
            for (UINT row = 0; row < numRows; ++row) {
                std::memcpy(
                    mappedData + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                    decodedImage.pixels.data() + static_cast<std::size_t>(row) * sourceRowPitch,
                    sourceRowPitch);
            }

            D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(uploadBufferSize)};
            cachedTexture.uploadResource->Unmap(0, &writtenRange);

            D3D12_TEXTURE_COPY_LOCATION srcLocation{};
            srcLocation.pResource = cachedTexture.uploadResource.Get();
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcLocation.PlacedFootprint = footprint;

            D3D12_TEXTURE_COPY_LOCATION dstLocation{};
            dstLocation.pResource = cachedTexture.resource.Get();
            dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLocation.SubresourceIndex = 0;

            commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

            D3D12_RESOURCE_BARRIER textureBarrier{};
            textureBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            textureBarrier.Transition.pResource = cachedTexture.resource.Get();
            textureBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            textureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            textureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            commandList->ResourceBarrier(1, &textureBarrier);

            if (!AllocateSrvDescriptor(cachedTexture.srvCpuDescriptor, cachedTexture.srvGpuDescriptor)) {
                outError = "Failed to allocate SRV descriptor for model texture.";
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            device->CreateShaderResourceView(cachedTexture.resource.Get(), &srvDesc, cachedTexture.srvCpuDescriptor);
        }

        return !modelTextures.empty();
    }

    bool CreateDepthStencilBuffer(UINT width, UINT height, std::string& outError) {
        if (!device || !dsvHeap || width == 0 || height == 0) {
            outError = "Invalid device/DSV heap or depth dimensions.";
            return false;
        }

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Alignment = 0;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = kDepthFormat;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.SampleDesc.Quality = 0;
        depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = kDepthFormat;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        HRESULT result = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue,
            IID_PPV_ARGS(&depthStencilBuffer));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateCommittedResource for depth-stencil buffer failed", result);
            return false;
        }
        TrackDebugObject(depthStencilBuffer.Get(), "DepthStencilBuffer");

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = kDepthFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        dsvDesc.Texture2D.MipSlice = 0;

        dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CreateDepthStencilView(depthStencilBuffer.Get(), &dsvDesc, dsvHandle);
        return true;
    }

    bool Initialize(SDL_Window* sdlWindow, std::string& outError) {
        window = sdlWindow;

        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        hwnd = static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd) {
            outError = "Failed to acquire HWND from SDL window properties.";
            return false;
        }

        bool debugLayerEnabled = false;
    #if defined(_DEBUG)
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            debugLayerEnabled = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "D3D12 debug layer enabled for native renderer.");
            }
        }
    #endif

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInitialized = SUCCEEDED(comResult);

        UINT dxgiFlags = debugLayerEnabled ? DXGI_CREATE_FACTORY_DEBUG : 0;
        HRESULT result = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateDXGIFactory2 failed", result);
            return false;
        }

        result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        if (FAILED(result)) {
            outError = ToErrorMessage("D3D12CreateDevice failed", result);
            return false;
        }

        (void)device.As(&infoQueue);

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        result = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateCommandQueue failed", result);
            return false;
        }
        TrackDebugObject(commandQueue.Get(), "MainCommandQueue");

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
        swapChainDesc.BufferCount = kFrameCount;
        swapChainDesc.Width = static_cast<UINT>(windowWidth > 0 ? windowWidth : 1280);
        swapChainDesc.Height = static_cast<UINT>(windowHeight > 0 ? windowHeight : 720);
        swapChainDesc.Format = kBackbufferFormat;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> baseSwapChain;
        result = factory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &baseSwapChain);
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateSwapChainForHwnd failed", result);
            return false;
        }

        result = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(result)) {
            outError = ToErrorMessage("MakeWindowAssociation failed", result);
            return false;
        }

        result = baseSwapChain.As(&swapChain);
        if (FAILED(result)) {
            outError = ToErrorMessage("Query IDXGISwapChain3 failed", result);
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = kFrameCount;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        result = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
        if (FAILED(result)) {
            outError = ToErrorMessage("Create RTV descriptor heap failed", result);
            return false;
        }
        TrackDebugObject(rtvHeap.Get(), "RtvDescriptorHeap");

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        result = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap));
        if (FAILED(result)) {
            outError = ToErrorMessage("Create DSV descriptor heap failed", result);
            return false;
        }
        TrackDebugObject(dsvHeap.Get(), "DsvDescriptorHeap");
        dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < kFrameCount; ++index) {
            result = swapChain->GetBuffer(index, IID_PPV_ARGS(&frames[index].backBuffer));
            if (FAILED(result)) {
                outError = ToErrorMessage("GetBuffer failed", result);
                return false;
            }
            TrackDebugObject(frames[index].backBuffer.Get(), "SwapchainBackBuffer[" + std::to_string(index) + "]");

            device->CreateRenderTargetView(frames[index].backBuffer.Get(), nullptr, rtvHandle);
            frames[index].rtvHandle = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;

            result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[index].commandAllocator));
            if (FAILED(result)) {
                outError = ToErrorMessage("CreateCommandAllocator failed", result);
                return false;
            }
            TrackDebugObject(frames[index].commandAllocator.Get(), "CommandAllocator[" + std::to_string(index) + "]");
        }

        if (!CreateDepthStencilBuffer(swapChainDesc.Width, swapChainDesc.Height, outError)) {
            return false;
        }

        result = device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            frames[0].commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateCommandList failed", result);
            return false;
        }
        TrackDebugObject(commandList.Get(), "MainCommandList");
        commandList->Close();

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.NumDescriptors = kSrvDescriptorCount;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        result = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap));
        if (FAILED(result)) {
            outError = ToErrorMessage("Create SRV descriptor heap failed", result);
            return false;
        }
        TrackDebugObject(srvHeap.Get(), "SrvDescriptorHeap");

        srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        srvNextFreeIndex = 1;
        srvFreeList.clear();

        fontSrvCpuDescriptor = srvHeap->GetCPUDescriptorHandleForHeapStart();
        fontSrvGpuDescriptor = srvHeap->GetGPUDescriptorHandleForHeapStart();

        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        if (FAILED(result)) {
            outError = ToErrorMessage("CreateFence failed", result);
            return false;
        }
        TrackDebugObject(fence.Get(), "MainFence");

        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent) {
            outError = "CreateEvent for fence synchronization failed.";
            return false;
        }

        if (!CreateWirePipeline(outError)) {
            return false;
        }

        if (!CreateTexturedPipeline(outError)) {
            return false;
        }

        return true;
    }

    void LogDebugMessages(const char* stage) {
        if (!infoQueue) {
            return;
        }

        const UINT64 messageCount = infoQueue->GetNumStoredMessages();
        if (messageCount == 0) {
            return;
        }

        for (UINT64 index = 0; index < messageCount; ++index) {
            SIZE_T messageLength = 0;
            if (FAILED(infoQueue->GetMessage(index, nullptr, &messageLength))) {
                continue;
            }

            std::vector<char> messageBytes(messageLength);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageBytes.data());
            if (FAILED(infoQueue->GetMessage(index, message, &messageLength))) {
                continue;
            }

            SDL_LogError(
                SDL_LOG_CATEGORY_APPLICATION,
                "[D3D12 %s] %s%s",
                stage,
                message->pDescription ? message->pDescription : "(no description)",
                DescribeTrackedObjectFromMessage(message->pDescription).c_str());
        }

        infoQueue->ClearStoredMessages();
    }

    void Shutdown() noexcept {
        WaitForGpuIdle();
        ReleaseModelTextures();

        for (FrameContext& frame : frames) {
            frame.backBuffer.Reset();
            frame.commandAllocator.Reset();
            frame.fenceValue = 0;
        }

        if (fenceEvent) {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }

        fence.Reset();
        infoQueue.Reset();
        srvHeap.Reset();
        dsvHeap.Reset();
        rtvHeap.Reset();
        wireVertexBuffer.Reset();
        texturedVertexBuffer.Reset();
        depthStencilBuffer.Reset();
        texturedTransparentPipelineState.Reset();
        texturedOpaquePipelineState.Reset();
        texturedRootSignature.Reset();
        wirePipelineState.Reset();
        wireRootSignature.Reset();
        commandList.Reset();
        swapChain.Reset();
        commandQueue.Reset();
        device.Reset();
        factory.Reset();
        hwnd = nullptr;
        window = nullptr;
        frameIndex = 0;
        nextFenceValue = 1;
        fontSrvCpuDescriptor = {};
        fontSrvGpuDescriptor = {};
        dsvHandle = {};
        srvDescriptorSize = 0;
        dsvDescriptorSize = 0;
        srvNextFreeIndex = 1;
        srvFreeList.clear();
        modelTexturePaths.clear();
        modelTextures.clear();
        debugObjectNames.clear();
        wireVertexCapacity = 0;
        texturedVertexCapacity = 0;
        wireVertexBufferView = {};
        texturedVertexBufferView = {};

        if (comInitialized) {
            CoUninitialize();
            comInitialized = false;
        }
    }

    void WaitForGpuIdle() noexcept {
        if (!commandQueue || !fence || !fenceEvent) {
            return;
        }

        const UINT64 fenceToWait = nextFenceValue++;
        commandQueue->Signal(fence.Get(), fenceToWait);
        if (fence->GetCompletedValue() < fenceToWait) {
            fence->SetEventOnCompletion(fenceToWait, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }


    bool AllocateSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle) {
        if (!srvHeap || srvDescriptorSize == 0) {
            return false;
        }

        UINT descriptorIndex = 0;
        if (!srvFreeList.empty()) {
            descriptorIndex = srvFreeList.back();
            srvFreeList.pop_back();
        } else {
            if (srvNextFreeIndex >= kSrvDescriptorCount) {
                return false;
            }
            descriptorIndex = srvNextFreeIndex++;
        }

        outCpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        outCpuHandle.ptr += static_cast<SIZE_T>(descriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize);

        outGpuHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
        outGpuHandle.ptr += static_cast<UINT64>(descriptorIndex) * static_cast<UINT64>(srvDescriptorSize);
        return true;
    }

    void FreeSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
        (void)gpuHandle;
        if (!srvHeap || srvDescriptorSize == 0 || cpuHandle.ptr == 0) {
            return;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE baseHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        if (cpuHandle.ptr < baseHandle.ptr) {
            return;
        }

        const SIZE_T offset = cpuHandle.ptr - baseHandle.ptr;
        const UINT descriptorIndex = static_cast<UINT>(offset / srvDescriptorSize);
        if (descriptorIndex < kSrvDescriptorCount) {
            srvFreeList.push_back(descriptorIndex);
        }
    }
    void BeginFrame() {
        frameIndex = swapChain->GetCurrentBackBufferIndex();
        FrameContext& frame = frames[frameIndex];

        if (fence->GetCompletedValue() < frame.fenceValue) {
            fence->SetEventOnCompletion(frame.fenceValue, fenceEvent);
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        frame.commandAllocator->Reset();
        commandList->Reset(frame.commandAllocator.Get(), nullptr);

        D3D12_RESOURCE_BARRIER toRenderTarget{};
        toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRenderTarget.Transition.pResource = frame.backBuffer.Get();
        toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &toRenderTarget);

        commandList->OMSetRenderTargets(1, &frame.rtvHandle, FALSE, &dsvHandle);

        const float clearColor[4] = {0.07f, 0.08f, 0.09f, 1.0f};
        commandList->ClearRenderTargetView(frame.rtvHandle, clearColor, 0, nullptr);
        commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        int viewportWidth = 0;
        int viewportHeight = 0;
        SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);

        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(viewportWidth > 1 ? viewportWidth : 1);
        viewport.Height = static_cast<float>(viewportHeight > 1 ? viewportHeight : 1);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        commandList->RSSetViewports(1, &viewport);

        D3D12_RECT scissorRect{};
        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = static_cast<LONG>(viewport.Width);
        scissorRect.bottom = static_cast<LONG>(viewport.Height);
        commandList->RSSetScissorRects(1, &scissorRect);
    }

    void EndFrame() {
        FrameContext& frame = frames[frameIndex];

        LogDebugMessages("pre-close");

        D3D12_RESOURCE_BARRIER toPresent{};
        toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toPresent.Transition.pResource = frame.backBuffer.Get();
        toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList->ResourceBarrier(1, &toPresent);

        commandList->Close();
        ID3D12CommandList* commandLists[] = {commandList.Get()};
        commandQueue->ExecuteCommandLists(1, commandLists);
        swapChain->Present(1, 0);

        LogDebugMessages("post-present");

        const UINT64 fenceValue = nextFenceValue++;
        commandQueue->Signal(fence.Get(), fenceValue);
        frame.fenceValue = fenceValue;
    }

    void RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance, bool wireOverlayEnabled) {
        if (!commandList || !wirePipelineState || !wireRootSignature || !texturedOpaquePipelineState || !texturedTransparentPipelineState || !texturedRootSignature || !model.IsValid()) {
            return;
        }

        int viewportWidth = 0;
        int viewportHeight = 0;
        SDL_GetWindowSize(window, &viewportWidth, &viewportHeight);
        if (viewportWidth <= 1 || viewportHeight <= 1) {
            return;
        }

        const float aspectRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
        const float clampedDistance = std::clamp(cameraDistance, 1.0f, 20.0f);

        glm::mat4 modelMatrix(1.0f);
        modelMatrix = glm::rotate(modelMatrix, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(rollDegrees), glm::vec3(0.0f, 0.0f, 1.0f));

        const glm::mat4 viewMatrix = glm::lookAt(
            glm::vec3(0.0f, 0.0f, clampedDistance),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));

        const glm::mat4 projectionMatrix = glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 100.0f);
        const glm::mat4 mvp = projectionMatrix * viewMatrix * modelMatrix;

        std::vector<ClipVertex> projected;
        projected.reserve(model.positions.size());
        for (const glm::vec3& point : model.positions) {
            projected.push_back(ProjectToNdc(point, mvp));
        }

        std::vector<WireVertex> lineVertices;
        lineVertices.reserve(model.indices.size() * 2);

        const bool canRenderTextured =
            model.texCoords.size() == model.positions.size() &&
            !model.texturePaths.empty() &&
            !model.submeshes.empty();

        const std::size_t indexCount = model.indices.size();
        for (std::size_t index = 0; index + 2 < indexCount; index += 3) {
            const std::uint32_t i0 = model.indices[index];
            const std::uint32_t i1 = model.indices[index + 1];
            const std::uint32_t i2 = model.indices[index + 2];
            if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size()) {
                continue;
            }

            AddLine(lineVertices, projected[i0], projected[i1]);
            AddLine(lineVertices, projected[i1], projected[i2]);
            AddLine(lineVertices, projected[i2], projected[i0]);

        }

        if (lineVertices.empty() && !canRenderTextured) {
            return;
        }

        bool renderedAnyTexturedGeometry = false;
        if (canRenderTextured) {
            std::string textureError;
            if (EnsureModelTexturesUploaded(model, textureError)) {
                struct TexturedTriangle {
                    TexturedVertex vertices[3];
                    D3D12_GPU_DESCRIPTOR_HANDLE colorTextureHandle{};
                    D3D12_GPU_DESCRIPTOR_HANDLE opacityTextureHandle{};
                    float depthKey = 0.0f;
                    bool isTransparent = false;
                };

                std::vector<TexturedTriangle> texturedTriangles;
                texturedTriangles.reserve(model.indices.size() / 3);

                for (const ModelSubmesh& submesh : model.submeshes) {
                    if (submesh.textureIndex < 0 || static_cast<std::size_t>(submesh.textureIndex) >= modelTextures.size()) {
                        continue;
                    }

                    const CachedModelTexture& texture = modelTextures[static_cast<std::size_t>(submesh.textureIndex)];
                    if (texture.srvGpuDescriptor.ptr == 0 || submesh.indexCount < 3) {
                        continue;
                    }

                    const CachedModelTexture* opacityTexture = nullptr;
                    if (submesh.opacityTextureIndex >= 0 && static_cast<std::size_t>(submesh.opacityTextureIndex) < modelTextures.size()) {
                        const CachedModelTexture& opacityTextureCandidate = modelTextures[static_cast<std::size_t>(submesh.opacityTextureIndex)];
                        if (opacityTextureCandidate.srvGpuDescriptor.ptr != 0) {
                            opacityTexture = &opacityTextureCandidate;
                        }
                    }

                    const float submeshOpacity = std::clamp(submesh.opacity, 0.0f, 1.0f);
                    const bool submeshIsCutout = submesh.alphaCutoutEnabled;
                    const float submeshCutoff = std::clamp(submesh.alphaCutoff, 0.0f, 1.0f);
                    const float encodedCutoff = submesh.opacityTextureInverted ? -submeshCutoff : submeshCutoff;
                    const bool submeshIsTransparent =
                        submeshIsCutout ||
                        submesh.isTransparent ||
                        texture.hasTransparency ||
                        (opacityTexture && opacityTexture->hasTransparency) ||
                        submeshOpacity < 0.999f;

                    const float encodedOpacity = submeshIsCutout ? -submeshOpacity : submeshOpacity;

                    const std::size_t indexStart = static_cast<std::size_t>(submesh.indexStart);
                    const std::size_t indexEnd = indexStart + static_cast<std::size_t>(submesh.indexCount);
                    if (indexEnd > model.indices.size()) {
                        continue;
                    }

                    for (std::size_t index = indexStart; index + 2 < indexEnd; index += 3) {
                        const std::uint32_t i0 = model.indices[index];
                        const std::uint32_t i1 = model.indices[index + 1];
                        const std::uint32_t i2 = model.indices[index + 2];
                        if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size()) {
                            continue;
                        }
                        if (i0 >= model.texCoords.size() || i1 >= model.texCoords.size() || i2 >= model.texCoords.size()) {
                            continue;
                        }

                        const ClipVertex& p0 = projected[i0];
                        const ClipVertex& p1 = projected[i1];
                        const ClipVertex& p2 = projected[i2];
                        if (!p0.valid || !p1.valid || !p2.valid) {
                            continue;
                        }

                        const glm::vec2& uv0 = model.texCoords[i0];
                        const glm::vec2& uv1 = model.texCoords[i1];
                        const glm::vec2& uv2 = model.texCoords[i2];

                        TexturedTriangle triangle{};
                        triangle.vertices[0] = {{p0.x, p0.y, p0.z, 1.0f}, {1.0f - uv0.x, 1.0f - uv0.y}, encodedOpacity, encodedCutoff};
                        triangle.vertices[1] = {{p1.x, p1.y, p1.z, 1.0f}, {1.0f - uv1.x, 1.0f - uv1.y}, encodedOpacity, encodedCutoff};
                        triangle.vertices[2] = {{p2.x, p2.y, p2.z, 1.0f}, {1.0f - uv2.x, 1.0f - uv2.y}, encodedOpacity, encodedCutoff};
                        triangle.colorTextureHandle = texture.srvGpuDescriptor;
                        triangle.opacityTextureHandle = opacityTexture ? opacityTexture->srvGpuDescriptor : texture.srvGpuDescriptor;
                        triangle.depthKey = (p0.z + p1.z + p2.z) / 3.0f;
                        triangle.isTransparent = submeshIsTransparent;
                        texturedTriangles.push_back(triangle);
                    }
                }

                std::sort(texturedTriangles.begin(), texturedTriangles.end(), [](const TexturedTriangle& left, const TexturedTriangle& right) {
                    if (left.isTransparent != right.isTransparent) {
                        return !left.isTransparent;
                    }

                    if (left.isTransparent) {
                        return left.depthKey > right.depthKey;
                    }

                    return left.depthKey < right.depthKey;
                });

                const UINT texturedVertexCount = static_cast<UINT>(texturedTriangles.size() * 3);
                if (texturedVertexCount > 0) {
                    std::string allocationError;
                    if (!EnsureTexturedVertexBuffer(texturedVertexCount, allocationError)) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", allocationError.c_str());
                        texturedTriangles.clear();
                    }
                }

                ID3D12DescriptorHeap* descriptorHeaps[] = {srvHeap.Get()};
                commandList->SetDescriptorHeaps(1, descriptorHeaps);
                commandList->SetGraphicsRootSignature(texturedRootSignature.Get());
                commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                if (!texturedTriangles.empty() && texturedVertexCount > 0) {
                    void* mappedData = nullptr;
                    D3D12_RANGE readRange{0, 0};
                    if (SUCCEEDED(texturedVertexBuffer->Map(0, &readRange, &mappedData)) && mappedData) {
                        auto* destinationVertices = static_cast<TexturedVertex*>(mappedData);
                        for (std::size_t triangleIndex = 0; triangleIndex < texturedTriangles.size(); ++triangleIndex) {
                            destinationVertices[triangleIndex * 3 + 0] = texturedTriangles[triangleIndex].vertices[0];
                            destinationVertices[triangleIndex * 3 + 1] = texturedTriangles[triangleIndex].vertices[1];
                            destinationVertices[triangleIndex * 3 + 2] = texturedTriangles[triangleIndex].vertices[2];
                        }
                        const std::size_t uploadBytes = static_cast<std::size_t>(texturedVertexCount) * sizeof(TexturedVertex);
                        D3D12_RANGE writtenRange{0, uploadBytes};
                        texturedVertexBuffer->Unmap(0, &writtenRange);

                        D3D12_VERTEX_BUFFER_VIEW vbView = texturedVertexBufferView;
                        vbView.SizeInBytes = static_cast<UINT>(uploadBytes);
                        commandList->IASetVertexBuffers(0, 1, &vbView);

                        bool currentTransparencyState = false;
                        bool hasCurrentTransparencyState = false;
                        std::size_t firstTriangleInBatch = 0;
                        while (firstTriangleInBatch < texturedTriangles.size()) {
                            const D3D12_GPU_DESCRIPTOR_HANDLE currentColorTexture = texturedTriangles[firstTriangleInBatch].colorTextureHandle;
                            const D3D12_GPU_DESCRIPTOR_HANDLE currentOpacityTexture = texturedTriangles[firstTriangleInBatch].opacityTextureHandle;
                            const bool batchIsTransparent = texturedTriangles[firstTriangleInBatch].isTransparent;
                            std::size_t endTriangleInBatch = firstTriangleInBatch + 1;
                            while (endTriangleInBatch < texturedTriangles.size() &&
                                texturedTriangles[endTriangleInBatch].colorTextureHandle.ptr == currentColorTexture.ptr &&
                                texturedTriangles[endTriangleInBatch].opacityTextureHandle.ptr == currentOpacityTexture.ptr &&
                                texturedTriangles[endTriangleInBatch].isTransparent == batchIsTransparent) {
                                ++endTriangleInBatch;
                            }

                            if (!hasCurrentTransparencyState || currentTransparencyState != batchIsTransparent) {
                                commandList->SetPipelineState(batchIsTransparent ? texturedTransparentPipelineState.Get() : texturedOpaquePipelineState.Get());
                                currentTransparencyState = batchIsTransparent;
                                hasCurrentTransparencyState = true;
                            }

                            const UINT startVertex = static_cast<UINT>(firstTriangleInBatch * 3);
                            const UINT vertexCount = static_cast<UINT>((endTriangleInBatch - firstTriangleInBatch) * 3);

                            commandList->SetGraphicsRootDescriptorTable(0, currentColorTexture);
                            commandList->SetGraphicsRootDescriptorTable(1, currentOpacityTexture);
                            commandList->DrawInstanced(vertexCount, 1, startVertex, 0);

                            firstTriangleInBatch = endTriangleInBatch;
                        }

                        renderedAnyTexturedGeometry = true;
                    }
                }
            } else if (!textureError.empty()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Native DX12 model texture disabled: %s", textureError.c_str());
            }
        }

        const bool shouldDrawWireOverlay = wireOverlayEnabled || !renderedAnyTexturedGeometry;
        if (!shouldDrawWireOverlay || lineVertices.empty()) {
            return;
        }

        std::string allocationError;
        if (!EnsureWireVertexBuffer(static_cast<UINT>(lineVertices.size()), allocationError)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", allocationError.c_str());
            return;
        }

        void* mappedData = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(wireVertexBuffer->Map(0, &readRange, &mappedData)) || !mappedData) {
            return;
        }

        const std::size_t uploadBytes = lineVertices.size() * sizeof(WireVertex);
        std::memcpy(mappedData, lineVertices.data(), uploadBytes);
        D3D12_RANGE writtenRange{0, uploadBytes};
        wireVertexBuffer->Unmap(0, &writtenRange);

        D3D12_VERTEX_BUFFER_VIEW vbView = wireVertexBufferView;
        vbView.SizeInBytes = static_cast<UINT>(uploadBytes);

        commandList->SetPipelineState(wirePipelineState.Get());
        commandList->SetGraphicsRootSignature(wireRootSignature.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
        commandList->IASetVertexBuffers(0, 1, &vbView);
        commandList->DrawInstanced(static_cast<UINT>(lineVertices.size()), 1, 0, 0);
    }
};
#endif

NativeDx12Renderer::NativeDx12Renderer()
    : impl_(
#if defined(_WIN32)
        std::make_unique<Impl>()
#else
        nullptr
#endif
    ) {}

NativeDx12Renderer::~NativeDx12Renderer() = default;

bool NativeDx12Renderer::Initialize(SDL_Window* window, std::string& outError) {
#if defined(_WIN32)
    return impl_ ? impl_->Initialize(window, outError) : false;
#else
    (void)window;
    outError = "Native DirectX 12 renderer is only supported on Windows.";
    return false;
#endif
}

void NativeDx12Renderer::Shutdown() noexcept {
#if defined(_WIN32)
    if (impl_) {
        impl_->Shutdown();
    }
#endif
}

void NativeDx12Renderer::BeginFrame() {
#if defined(_WIN32)
    if (impl_) {
        impl_->BeginFrame();
    }
#endif
}

void NativeDx12Renderer::EndFrame() {
#if defined(_WIN32)
    if (impl_) {
        impl_->EndFrame();
    }
#endif
}

void NativeDx12Renderer::RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance, bool wireOverlayEnabled) {
#if defined(_WIN32)
    if (impl_) {
        impl_->RenderModelWireframe(model, yawDegrees, pitchDegrees, rollDegrees, cameraDistance, wireOverlayEnabled);
    }
#else
    (void)model;
    (void)yawDegrees;
    (void)pitchDegrees;
    (void)rollDegrees;
    (void)cameraDistance;
    (void)wireOverlayEnabled;
#endif
}

SDL_Renderer* NativeDx12Renderer::GetNativeRenderer() const noexcept {
    return nullptr;
}

const char* NativeDx12Renderer::GetName() const noexcept {
    return "DirectX 12 Native";
}

#if defined(_WIN32)
ID3D12Device* NativeDx12Renderer::GetDevice() const noexcept {
    return impl_ ? impl_->device.Get() : nullptr;
}

ID3D12CommandQueue* NativeDx12Renderer::GetCommandQueue() const noexcept {
    return impl_ ? impl_->commandQueue.Get() : nullptr;
}

ID3D12GraphicsCommandList* NativeDx12Renderer::GetCommandList() const noexcept {
    return impl_ ? impl_->commandList.Get() : nullptr;
}

ID3D12DescriptorHeap* NativeDx12Renderer::GetSrvDescriptorHeap() const noexcept {
    return impl_ ? impl_->srvHeap.Get() : nullptr;
}

bool NativeDx12Renderer::AllocateSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle) {
    return impl_ ? impl_->AllocateSrvDescriptor(outCpuHandle, outGpuHandle) : false;
}

void NativeDx12Renderer::WaitForGpuIdle() noexcept {
    if (impl_) {
        impl_->WaitForGpuIdle();
    }
}

void NativeDx12Renderer::FreeSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    if (impl_) {
        impl_->FreeSrvDescriptor(cpuHandle, gpuHandle);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE NativeDx12Renderer::GetFontSrvCpuDescriptor() const noexcept {
    return impl_ ? impl_->fontSrvCpuDescriptor : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE NativeDx12Renderer::GetFontSrvGpuDescriptor() const noexcept {
    return impl_ ? impl_->fontSrvGpuDescriptor : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

DXGI_FORMAT NativeDx12Renderer::GetRtvFormat() const noexcept {
    return kBackbufferFormat;
}

int NativeDx12Renderer::GetFramesInFlight() const noexcept {
    return static_cast<int>(kFrameCount);
}
#endif
}
