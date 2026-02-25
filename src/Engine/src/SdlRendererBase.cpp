#include "SdlRendererBase.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include <SDL3/SDL.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#if defined(_WIN32)
#include <objbase.h>
#include <wincodec.h>
#include <windows.h>
#endif

namespace engine {
namespace {
struct ProjectedVertex {
    float x;
    float y;
    float depth;
    bool valid;
};

ProjectedVertex ProjectVertex(const glm::vec3& point, const glm::mat4& mvp, int width, int height) {
    const glm::vec4 clip = mvp * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0001f) {
        return {0.0f, 0.0f, 1.0f, false};
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const float screenX = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
    const float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
    const bool depthValid = ndc.z >= -1.0f && ndc.z <= 1.0f;
    return {screenX, screenY, ndc.z, depthValid};
}

struct TexturedTriangle {
    SDL_Vertex vertices[3];
    SDL_Texture* texture;
    float depth;
    bool isTransparent;
};

std::uint8_t SampleSurfaceChannelNearest(const SDL_Surface* surface, int x, int y, int channelIndex) {
    if (!surface || !surface->pixels || surface->w <= 0 || surface->h <= 0 || channelIndex < 0 || channelIndex > 3) {
        return 255;
    }

    const int sampleX = std::clamp(x, 0, surface->w - 1);
    const int sampleY = std::clamp(y, 0, surface->h - 1);
    const std::uint8_t* pixelBase = static_cast<const std::uint8_t*>(surface->pixels) +
        (sampleY * surface->pitch) + (sampleX * 4);
    return pixelBase[channelIndex];
}

#if defined(_WIN32)
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

SDL_Surface* LoadSurfaceWithWic(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }

    const std::wstring widePath = Utf8ToWide(path);
    if (widePath.empty()) {
        return nullptr;
    }

    IWICImagingFactory* factory = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result) || !factory) {
        return nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    result = factory->CreateDecoderFromFilename(
        widePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(result) || !decoder) {
        factory->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result) || !frame) {
        decoder->Release();
        factory->Release();
        return nullptr;
    }

    IWICFormatConverter* converter = nullptr;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result) || !converter) {
        frame->Release();
        decoder->Release();
        factory->Release();
        return nullptr;
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
        return nullptr;
    }

    UINT width = 0;
    UINT height = 0;
    result = converter->GetSize(&width, &height);
    if (FAILED(result) || width == 0 || height == 0) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return nullptr;
    }

    const UINT stride = width * 4;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
    result = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(result)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateSurface(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
    if (surface && static_cast<UINT>(surface->pitch) == stride) {
        std::memcpy(surface->pixels, pixels.data(), pixels.size());
    } else if (surface) {
        for (UINT row = 0; row < height; ++row) {
            const std::size_t sourceOffset = static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
            const std::size_t destinationOffset = static_cast<std::size_t>(row) * static_cast<std::size_t>(surface->pitch);
            std::memcpy(
                static_cast<std::uint8_t*>(surface->pixels) + destinationOffset,
                pixels.data() + sourceOffset,
                static_cast<std::size_t>(stride));
        }
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return surface;
}
#endif
}

SdlRendererBase::SdlRendererBase(const char* rendererHint, const char* displayName)
    : rendererHint_(rendererHint),
    displayName_(displayName),
    renderer_(nullptr),
    modelTextures_(),
    modelTextureSurfaces_(),
    modelTexturePaths_(),
    composedTextures_()
#if defined(_WIN32)
    , comInitialized_(false)
#endif
{}

SdlRendererBase::~SdlRendererBase() {
    Shutdown();
}

bool SdlRendererBase::Initialize(SDL_Window* window, std::string& outError) {
    if (!SDL_SetHint(SDL_HINT_RENDER_DRIVER, rendererHint_)) {
        outError = "Failed to set SDL render driver hint.";
        return false;
    }

    renderer_ = SDL_CreateRenderer(window, nullptr);
    if (!renderer_) {
        outError = SDL_GetError();
        return false;
    }

    const char* actualRendererName = SDL_GetRendererName(renderer_);

    SDL_Vertex vertices[3] = {};
    vertices[0].position = SDL_FPoint{8.0f, 8.0f};
    vertices[1].position = SDL_FPoint{40.0f, 8.0f};
    vertices[2].position = SDL_FPoint{8.0f, 40.0f};
    vertices[0].color = SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};
    vertices[1].color = SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};
    vertices[2].color = SDL_FColor{1.0f, 1.0f, 1.0f, 1.0f};

    SDL_ClearError();
    if (!SDL_RenderGeometry(renderer_, nullptr, vertices, 3, nullptr, 0)) {
        const char* geometryError = SDL_GetError();
        outError = "Renderer '" + std::string(actualRendererName ? actualRendererName : "unknown") +
            "' does not support SDL_RenderGeometry required by ImGui: " +
            (geometryError && geometryError[0] != '\0' ? std::string(geometryError) : "Unknown SDL error");
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
        return false;
    }

    SDL_SetRenderDrawColor(renderer_, 18, 20, 24, 255);
    SDL_RenderClear(renderer_);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
        "SDL renderer created. Requested='%s', actual='%s'.",
        rendererHint_,
        actualRendererName ? actualRendererName : "unknown");

#if defined(_WIN32)
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInitialized_ = SUCCEEDED(comResult);
#endif

    return true;
}

void SdlRendererBase::Shutdown() noexcept {
    ReleaseComposedTextures();
    ReleaseModelTextures();

    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }

#if defined(_WIN32)
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
#endif
}

bool SdlRendererBase::KeysEqual(const ComposedTextureKey& left, const ComposedTextureKey& right) noexcept {
    return left.colorTextureIndex == right.colorTextureIndex &&
        left.opacityTextureIndex == right.opacityTextureIndex &&
        left.opacityBits == right.opacityBits &&
        left.cutoffBits == right.cutoffBits &&
        left.useCutout == right.useCutout &&
        left.invertOpacityTexture == right.invertOpacityTexture;
}

void SdlRendererBase::BeginFrame() {
    SDL_SetRenderDrawColor(renderer_, 18, 20, 24, 255);
    SDL_RenderClear(renderer_);
}

void SdlRendererBase::EndFrame() {
    SDL_RenderPresent(renderer_);
}

void SdlRendererBase::RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance, bool wireOverlayEnabled) {
    if (!renderer_ || !model.IsValid()) {
        return;
    }

    int viewportWidth = 0;
    int viewportHeight = 0;
    SDL_GetRenderOutputSize(renderer_, &viewportWidth, &viewportHeight);
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

    std::vector<ProjectedVertex> projected;
    projected.reserve(model.positions.size());
    for (const glm::vec3& point : model.positions) {
        projected.push_back(ProjectVertex(point, mvp, viewportWidth, viewportHeight));
    }

    UpdateModelTextures(model);

    const bool canRenderTextured =
        !modelTextures_.empty() &&
        model.texCoords.size() == model.positions.size() &&
        !model.indices.empty();

    bool renderedAnyTexturedGeometry = false;

    if (canRenderTextured) {
        std::vector<TexturedTriangle> texturedTriangles;

        auto appendTriangles = [&](std::size_t indexStart, std::size_t indexEnd, SDL_Texture* texture, float opacity, bool isTransparent) {
            if (!texture || indexEnd > model.indices.size() || indexStart >= indexEnd) {
                return;
            }

            const float clampedOpacity = std::clamp(opacity, 0.0f, 1.0f);

            for (std::size_t index = indexStart; index + 2 < indexEnd; index += 3) {
                const std::uint32_t i0 = model.indices[index];
                const std::uint32_t i1 = model.indices[index + 1];
                const std::uint32_t i2 = model.indices[index + 2];

                if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size()) {
                    continue;
                }

                const ProjectedVertex& p0 = projected[i0];
                const ProjectedVertex& p1 = projected[i1];
                const ProjectedVertex& p2 = projected[i2];
                if (!p0.valid || !p1.valid || !p2.valid) {
                    continue;
                }

                TexturedTriangle triangle{};
                triangle.texture = texture;
                triangle.depth = (p0.depth + p1.depth + p2.depth) / 3.0f;
                triangle.isTransparent = isTransparent || clampedOpacity < 0.999f;

                const glm::vec2& uv0 = model.texCoords[i0];
                const glm::vec2& uv1 = model.texCoords[i1];
                const glm::vec2& uv2 = model.texCoords[i2];

                triangle.vertices[0].position = SDL_FPoint{p0.x, p0.y};
                triangle.vertices[1].position = SDL_FPoint{p1.x, p1.y};
                triangle.vertices[2].position = SDL_FPoint{p2.x, p2.y};

                triangle.vertices[0].color = SDL_FColor{1.0f, 1.0f, 1.0f, clampedOpacity};
                triangle.vertices[1].color = SDL_FColor{1.0f, 1.0f, 1.0f, clampedOpacity};
                triangle.vertices[2].color = SDL_FColor{1.0f, 1.0f, 1.0f, clampedOpacity};

                triangle.vertices[0].tex_coord = SDL_FPoint{1.0f - uv0.x, 1.0f - uv0.y};
                triangle.vertices[1].tex_coord = SDL_FPoint{1.0f - uv1.x, 1.0f - uv1.y};
                triangle.vertices[2].tex_coord = SDL_FPoint{1.0f - uv2.x, 1.0f - uv2.y};

                texturedTriangles.push_back(triangle);
            }
        };

        if (!model.submeshes.empty()) {
            for (const ModelSubmesh& submesh : model.submeshes) {
                SDL_Texture* texture = ResolveSubmeshTexture(model, submesh);
                if (!texture || submesh.indexCount < 3) {
                    continue;
                }

                const std::size_t indexStart = static_cast<std::size_t>(submesh.indexStart);
                const std::size_t indexEnd = indexStart + static_cast<std::size_t>(submesh.indexCount);
                const bool submeshUsesOpacityTexture = submesh.opacityTextureIndex >= 0;
                const bool submeshIsTransparent =
                    submesh.isTransparent ||
                    submesh.alphaCutoutEnabled ||
                    submeshUsesOpacityTexture ||
                    submesh.opacity < 0.999f;
                appendTriangles(indexStart, indexEnd, texture, submesh.opacity, submeshIsTransparent);
            }
        } else if (!modelTextures_.empty() && modelTextures_[0]) {
            appendTriangles(0, model.indices.size(), modelTextures_[0], 1.0f, false);
        }

        std::sort(
            texturedTriangles.begin(),
            texturedTriangles.end(),
            [](const TexturedTriangle& a, const TexturedTriangle& b) {
                if (a.isTransparent != b.isTransparent) {
                    return !a.isTransparent;
                }
                return a.depth > b.depth;
            });

        for (const TexturedTriangle& triangle : texturedTriangles) {
            SDL_RenderGeometry(renderer_, triangle.texture, triangle.vertices, 3, nullptr, 0);
        }

        renderedAnyTexturedGeometry = !texturedTriangles.empty();
    }

    const bool shouldDrawWireOverlay = wireOverlayEnabled || !renderedAnyTexturedGeometry;
    if (!shouldDrawWireOverlay) {
        return;
    }

    SDL_SetRenderDrawColor(renderer_, 176, 210, 255, 255);

    const std::size_t indexCount = model.indices.size();
    for (std::size_t index = 0; index + 2 < indexCount; index += 3) {
        const std::uint32_t i0 = model.indices[index];
        const std::uint32_t i1 = model.indices[index + 1];
        const std::uint32_t i2 = model.indices[index + 2];

        if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size()) {
            continue;
        }

        const ProjectedVertex& p0 = projected[i0];
        const ProjectedVertex& p1 = projected[i1];
        const ProjectedVertex& p2 = projected[i2];

        if (!p0.valid || !p1.valid || !p2.valid) {
            continue;
        }

        SDL_RenderLine(renderer_, p0.x, p0.y, p1.x, p1.y);
        SDL_RenderLine(renderer_, p1.x, p1.y, p2.x, p2.y);
        SDL_RenderLine(renderer_, p2.x, p2.y, p0.x, p0.y);
    }
}

void SdlRendererBase::UpdateModelTextures(const ModelData& model) {
    if (!renderer_) {
        return;
    }

    if (model.texturePaths.empty()) {
        ReleaseComposedTextures();
        ReleaseModelTextures();
        return;
    }

    if (modelTexturePaths_ == model.texturePaths && modelTextures_.size() == model.texturePaths.size()) {
        return;
    }

    ReleaseComposedTextures();
    ReleaseModelTextures();

    modelTextures_.reserve(model.texturePaths.size());
    modelTextureSurfaces_.reserve(model.texturePaths.size());
    modelTexturePaths_.reserve(model.texturePaths.size());

    for (const std::string& texturePath : model.texturePaths) {
        SDL_Surface* surface = nullptr;
#if defined(_WIN32)
        surface = LoadSurfaceWithWic(texturePath);
#endif

        if (!surface) {
            surface = SDL_LoadBMP(texturePath.c_str());
        }

        SDL_Texture* texture = nullptr;
        if (surface) {
            SDL_Surface* rgbaSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
            if (rgbaSurface) {
                SDL_DestroySurface(surface);
                surface = rgbaSurface;
            }

            texture = SDL_CreateTextureFromSurface(renderer_, surface);
        }

        if (texture) {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }

        modelTextures_.push_back(texture);
        modelTextureSurfaces_.push_back(surface);
        modelTexturePaths_.push_back(texturePath);
    }
}

SDL_Texture* SdlRendererBase::ResolveSubmeshTexture(const ModelData&, const ModelSubmesh& submesh) {
    if (submesh.textureIndex < 0 || static_cast<std::size_t>(submesh.textureIndex) >= modelTextures_.size()) {
        return nullptr;
    }

    const bool needsComposedTexture =
        submesh.opacityTextureIndex >= 0 ||
        submesh.alphaCutoutEnabled ||
        submesh.opacityTextureInverted ||
        submesh.opacity < 0.999f;
    if (!needsComposedTexture) {
        return modelTextures_[static_cast<std::size_t>(submesh.textureIndex)];
    }

    const ComposedTextureKey key{
        submesh.textureIndex,
        submesh.opacityTextureIndex,
        std::bit_cast<std::uint32_t>(std::clamp(submesh.opacity, 0.0f, 1.0f)),
        std::bit_cast<std::uint32_t>(std::clamp(submesh.alphaCutoff, 0.0f, 1.0f)),
        submesh.alphaCutoutEnabled,
        submesh.opacityTextureInverted};

    for (const ComposedTextureEntry& entry : composedTextures_) {
        if (entry.texture && KeysEqual(entry.key, key)) {
            return entry.texture;
        }
    }

    SDL_Texture* composedTexture = CreateComposedTexture(submesh);
    if (!composedTexture) {
        return modelTextures_[static_cast<std::size_t>(submesh.textureIndex)];
    }

    composedTextures_.push_back(ComposedTextureEntry{key, composedTexture});
    return composedTexture;
}

SDL_Texture* SdlRendererBase::CreateComposedTexture(const ModelSubmesh& submesh) {
    if (!renderer_ || submesh.textureIndex < 0 || static_cast<std::size_t>(submesh.textureIndex) >= modelTextureSurfaces_.size()) {
        return nullptr;
    }

    SDL_Surface* colorSurface = modelTextureSurfaces_[static_cast<std::size_t>(submesh.textureIndex)];
    if (!colorSurface || !colorSurface->pixels || colorSurface->w <= 0 || colorSurface->h <= 0) {
        return nullptr;
    }

    const SDL_Surface* opacitySurface = nullptr;
    if (submesh.opacityTextureIndex >= 0 && static_cast<std::size_t>(submesh.opacityTextureIndex) < modelTextureSurfaces_.size()) {
        opacitySurface = modelTextureSurfaces_[static_cast<std::size_t>(submesh.opacityTextureIndex)];
    }

    SDL_Surface* composedSurface = SDL_CreateSurface(colorSurface->w, colorSurface->h, SDL_PIXELFORMAT_RGBA32);
    if (!composedSurface || !composedSurface->pixels) {
        if (composedSurface) {
            SDL_DestroySurface(composedSurface);
        }
        return nullptr;
    }

    const float clampedOpacity = std::clamp(submesh.opacity, 0.0f, 1.0f);
    const float clampedCutoff = std::clamp(submesh.alphaCutoff, 0.0f, 1.0f);

    for (int y = 0; y < colorSurface->h; ++y) {
        std::uint8_t* dstRow = static_cast<std::uint8_t*>(composedSurface->pixels) + (y * composedSurface->pitch);
        for (int x = 0; x < colorSurface->w; ++x) {
            const std::uint8_t* srcPixel = static_cast<const std::uint8_t*>(colorSurface->pixels) +
                (y * colorSurface->pitch) + (x * 4);

            const int opacityX = opacitySurface ? (x * opacitySurface->w) / (std::max)(colorSurface->w, 1) : 0;
            const int opacityY = opacitySurface ? (y * opacitySurface->h) / (std::max)(colorSurface->h, 1) : 0;
            float opacitySample = opacitySurface ?
                static_cast<float>(SampleSurfaceChannelNearest(opacitySurface, opacityX, opacityY, 0)) / 255.0f :
                1.0f;
            if (submesh.opacityTextureInverted) {
                opacitySample = 1.0f - opacitySample;
            }

            const float colorAlpha = static_cast<float>(srcPixel[3]) / 255.0f;
            float finalAlpha = std::clamp(colorAlpha * clampedOpacity * std::clamp(opacitySample, 0.0f, 1.0f), 0.0f, 1.0f);
            if (submesh.alphaCutoutEnabled && finalAlpha < clampedCutoff) {
                finalAlpha = 0.0f;
            }

            std::uint8_t* dstPixel = dstRow + (x * 4);
            dstPixel[0] = srcPixel[0];
            dstPixel[1] = srcPixel[1];
            dstPixel[2] = srcPixel[2];
            dstPixel[3] = static_cast<std::uint8_t>(finalAlpha * 255.0f + 0.5f);
        }
    }

    SDL_Texture* composedTexture = SDL_CreateTextureFromSurface(renderer_, composedSurface);
    SDL_DestroySurface(composedSurface);
    if (!composedTexture) {
        return nullptr;
    }

    SDL_SetTextureBlendMode(composedTexture, SDL_BLENDMODE_BLEND);
    return composedTexture;
}

void SdlRendererBase::ReleaseComposedTextures() noexcept {
    for (ComposedTextureEntry& entry : composedTextures_) {
        if (entry.texture) {
            SDL_DestroyTexture(entry.texture);
            entry.texture = nullptr;
        }
    }
    composedTextures_.clear();
}

void SdlRendererBase::ReleaseModelTextures() noexcept {
    for (SDL_Texture* texture : modelTextures_) {
        if (texture) {
            SDL_DestroyTexture(texture);
        }
    }
    modelTextures_.clear();

    for (SDL_Surface* surface : modelTextureSurfaces_) {
        if (surface) {
            SDL_DestroySurface(surface);
        }
    }
    modelTextureSurfaces_.clear();
    modelTexturePaths_.clear();
}

SDL_Renderer* SdlRendererBase::GetNativeRenderer() const noexcept {
    return renderer_;
}

const char* SdlRendererBase::GetName() const noexcept {
    return displayName_;
}
}
