#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Engine/ModelData.hpp"

struct SDL_Renderer;
struct SDL_Surface;
struct SDL_Texture;
struct SDL_Window;

namespace engine {
class SdlRendererBase {
public:
    SdlRendererBase(const char* rendererHint, const char* displayName);
    ~SdlRendererBase();

    bool Initialize(SDL_Window* window, std::string& outError);
    void Shutdown() noexcept;

    void BeginFrame();
    void EndFrame();

    void RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance, bool wireOverlayEnabled);

    [[nodiscard]] SDL_Renderer* GetNativeRenderer() const noexcept;
    [[nodiscard]] const char* GetName() const noexcept;

private:
    struct ComposedTextureKey {
        std::int32_t colorTextureIndex;
        std::int32_t opacityTextureIndex;
        std::uint32_t opacityBits;
        std::uint32_t cutoffBits;
        bool useCutout;
        bool invertOpacityTexture;
    };

    struct ComposedTextureEntry {
        ComposedTextureKey key;
        SDL_Texture* texture;
    };

    static bool KeysEqual(const ComposedTextureKey& left, const ComposedTextureKey& right) noexcept;
    void UpdateModelTextures(const ModelData& model);
    SDL_Texture* ResolveSubmeshTexture(const ModelData& model, const ModelSubmesh& submesh);
    SDL_Texture* CreateComposedTexture(const ModelSubmesh& submesh);
    void ReleaseComposedTextures() noexcept;
    void ReleaseModelTextures() noexcept;

    const char* rendererHint_;
    const char* displayName_;
    SDL_Renderer* renderer_;
    std::vector<SDL_Texture*> modelTextures_;
    std::vector<SDL_Surface*> modelTextureSurfaces_;
    std::vector<std::string> modelTexturePaths_;
    std::vector<ComposedTextureEntry> composedTextures_;
#if defined(_WIN32)
    bool comInitialized_;
#endif
};
}
