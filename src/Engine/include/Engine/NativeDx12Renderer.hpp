#pragma once

#include <memory>

#include "Engine/Renderer.hpp"

#if defined(_WIN32)
#include <d3d12.h>
#include <dxgiformat.h>
#endif

namespace engine {
class NativeDx12Renderer final : public Renderer {
public:
    NativeDx12Renderer();
    ~NativeDx12Renderer() override;

    bool Initialize(SDL_Window* window, std::string& outError) override;
    void Shutdown() noexcept override;
    void BeginFrame() override;
    void EndFrame() override;
    void RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance, bool wireOverlayEnabled) override;

    [[nodiscard]] SDL_Renderer* GetNativeRenderer() const noexcept override;
    [[nodiscard]] const char* GetName() const noexcept override;

#if defined(_WIN32)
    [[nodiscard]] ID3D12Device* GetDevice() const noexcept;
    [[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const noexcept;
    [[nodiscard]] ID3D12GraphicsCommandList* GetCommandList() const noexcept;
    [[nodiscard]] ID3D12DescriptorHeap* GetSrvDescriptorHeap() const noexcept;
    void WaitForGpuIdle() noexcept;
    bool AllocateSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle);
    void FreeSrvDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetFontSrvCpuDescriptor() const noexcept;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetFontSrvGpuDescriptor() const noexcept;
    [[nodiscard]] DXGI_FORMAT GetRtvFormat() const noexcept;
    [[nodiscard]] int GetFramesInFlight() const noexcept;
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
