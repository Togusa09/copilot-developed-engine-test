#pragma once

#include <string>

#include "Engine/ModelData.hpp"

struct SDL_Renderer;
struct SDL_Window;

namespace engine {
class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool Initialize(SDL_Window* window, std::string& outError) = 0;
    virtual void Shutdown() noexcept = 0;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    virtual void RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance) = 0;

    [[nodiscard]] virtual SDL_Renderer* GetNativeRenderer() const noexcept = 0;
    [[nodiscard]] virtual const char* GetName() const noexcept = 0;
};
}
