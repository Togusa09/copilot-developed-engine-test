#include "Engine/VulkanRenderer.hpp"

#include "SdlRendererBase.hpp"

namespace engine {
VulkanRenderer::VulkanRenderer()
    : impl_(std::make_unique<SdlRendererBase>("vulkan", "Vulkan")) {}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::Initialize(SDL_Window* window, std::string& outError) {
    return impl_->Initialize(window, outError);
}

void VulkanRenderer::Shutdown() noexcept {
    impl_->Shutdown();
}

void VulkanRenderer::BeginFrame() {
    impl_->BeginFrame();
}

void VulkanRenderer::EndFrame() {
    impl_->EndFrame();
}

void VulkanRenderer::RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance, bool wireOverlayEnabled) {
    impl_->RenderModelWireframe(model, yawDegrees, pitchDegrees, rollDegrees, cameraDistance, wireOverlayEnabled);
}

SDL_Renderer* VulkanRenderer::GetNativeRenderer() const noexcept {
    return impl_->GetNativeRenderer();
}

const char* VulkanRenderer::GetName() const noexcept {
    return impl_->GetName();
}
}
