#include "Engine/SoftwareRenderer.hpp"

#include "SdlRendererBase.hpp"

namespace engine {
SoftwareRenderer::SoftwareRenderer()
    : impl_(std::make_unique<SdlRendererBase>("software", "Software")) {}

SoftwareRenderer::~SoftwareRenderer() = default;

bool SoftwareRenderer::Initialize(SDL_Window* window, std::string& outError) {
    return impl_->Initialize(window, outError);
}

void SoftwareRenderer::Shutdown() noexcept {
    impl_->Shutdown();
}

void SoftwareRenderer::BeginFrame() {
    impl_->BeginFrame();
}

void SoftwareRenderer::EndFrame() {
    impl_->EndFrame();
}

void SoftwareRenderer::RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance, bool wireOverlayEnabled) {
    impl_->RenderModelWireframe(model, yawDegrees, pitchDegrees, rollDegrees, cameraDistance, wireOverlayEnabled);
}

SDL_Renderer* SoftwareRenderer::GetNativeRenderer() const noexcept {
    return impl_->GetNativeRenderer();
}

const char* SoftwareRenderer::GetName() const noexcept {
    return impl_->GetName();
}
}
