#pragma once

#include <memory>

#include "Engine/Renderer.hpp"

namespace engine {
class SdlRendererBase;

class VulkanRenderer final : public Renderer {
public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    bool Initialize(SDL_Window* window, std::string& outError) override;
    void Shutdown() noexcept override;
    void BeginFrame() override;
    void EndFrame() override;
    void RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance) override;

    [[nodiscard]] SDL_Renderer* GetNativeRenderer() const noexcept override;
    [[nodiscard]] const char* GetName() const noexcept override;

private:
    std::unique_ptr<SdlRendererBase> impl_;
};
}
