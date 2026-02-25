#include "Engine/DirectX12Renderer.hpp"

#include "SdlRendererBase.hpp"

namespace engine {
DirectX12Renderer::DirectX12Renderer()
    : impl_(std::make_unique<SdlRendererBase>("direct3d12", "DirectX 12")) {}

DirectX12Renderer::~DirectX12Renderer() = default;

bool DirectX12Renderer::Initialize(SDL_Window* window, std::string& outError) {
    return impl_->Initialize(window, outError);
}

void DirectX12Renderer::Shutdown() noexcept {
    impl_->Shutdown();
}

void DirectX12Renderer::BeginFrame() {
    impl_->BeginFrame();
}

void DirectX12Renderer::EndFrame() {
    impl_->EndFrame();
}

void DirectX12Renderer::RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance) {
    impl_->RenderModelWireframe(model, yawDegrees, pitchDegrees, rollDegrees, cameraDistance);
}

SDL_Renderer* DirectX12Renderer::GetNativeRenderer() const noexcept {
    return impl_->GetNativeRenderer();
}

const char* DirectX12Renderer::GetName() const noexcept {
    return impl_->GetName();
}
}
