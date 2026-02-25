#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "Engine/ModelData.hpp"

namespace engine {
class Renderer;

class Application {
public:
    Application();
    ~Application();

    int Run();
    void RequestExit() noexcept;
    bool IsRunning() const noexcept;

private:
    bool Initialize();
    void Shutdown() noexcept;
    bool CreateRenderer();
    bool InitializeImGui();
    void ShutdownImGui() noexcept;
    void UpdateGui();
    void DrawShortcutOverlay();
    void OpenLoadFbxDialog();
    void UpdateAnimationPlayback(float deltaSeconds);
    void StepAnimationSelection(int direction);

    bool running_;
    std::uint64_t frameCounter_;

    void* window_;
    std::unique_ptr<Renderer> renderer_;

    ModelData loadedModel_;
    std::string statusMessage_;

    float yawDegrees_;
    float pitchDegrees_;
    float rollDegrees_;
    float cameraDistance_;
    std::size_t currentAnimationIndex_;
    float animationTimeSeconds_;
    float animationSpeed_;
    bool animationPlaying_;
    std::uint64_t lastFrameCounterTimestamp_;

    bool sdlInitialized_;
    bool nfdInitialized_;
    bool imguiInitialized_;
    bool useNativeDx12ImGui_;
    bool wireOverlayEnabled_;
};
}
