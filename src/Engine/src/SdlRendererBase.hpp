#pragma once

#include <string>

#include "Engine/ModelData.hpp"

struct SDL_Renderer;
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

    void RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance);

    [[nodiscard]] SDL_Renderer* GetNativeRenderer() const noexcept;
    [[nodiscard]] const char* GetName() const noexcept;

private:
    const char* rendererHint_;
    const char* displayName_;
    SDL_Renderer* renderer_;
};
}
