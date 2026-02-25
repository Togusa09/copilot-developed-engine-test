#include "Engine/Application.hpp"

#include <memory>
#include <string>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <nfd.h>

#include "Engine/DirectX12Renderer.hpp"
#include "Engine/FbxLoader.hpp"
#include "Engine/Renderer.hpp"
#include "Engine/VulkanRenderer.hpp"

namespace engine {
Application::Application()
    : running_(true),
      frameCounter_(0),
      window_(nullptr),
      renderer_(nullptr),
      yawDegrees_(0.0f),
      pitchDegrees_(0.0f),
      rollDegrees_(0.0f),
      cameraDistance_(4.0f),
      sdlInitialized_(false),
      nfdInitialized_(false),
    imguiInitialized_(false) {}

Application::~Application() {
    Shutdown();
}

bool Application::Initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        statusMessage_ = SDL_GetError();
        return false;
    }
    sdlInitialized_ = true;

    SDL_Window* window = SDL_CreateWindow("EngineTest - FBX Viewer", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        statusMessage_ = SDL_GetError();
        return false;
    }
    window_ = window;

    if (NFD_Init() != NFD_OKAY) {
        statusMessage_ = "NativeFileDialog initialization failed.";
        return false;
    }
    nfdInitialized_ = true;

    if (!CreateRenderer()) {
        return false;
    }

    if (!InitializeImGui()) {
        return false;
    }

    statusMessage_ = "Ready. Load an FBX file from the UI.";
    return true;
}

bool Application::CreateRenderer() {
    SDL_Window* window = static_cast<SDL_Window*>(window_);
    std::string firstError;

    auto directXRenderer = std::make_unique<DirectX12Renderer>();
    if (directXRenderer->Initialize(window, firstError)) {
        renderer_ = std::move(directXRenderer);
        return true;
    }

    std::string secondError;
    auto vulkanRenderer = std::make_unique<VulkanRenderer>();
    if (vulkanRenderer->Initialize(window, secondError)) {
        renderer_ = std::move(vulkanRenderer);
        statusMessage_ = "DirectX 12 failed, using Vulkan fallback.";
        return true;
    }

    statusMessage_ = "Failed to create renderer. DX12: " + firstError + " | Vulkan: " + secondError;
    return false;
}

bool Application::InitializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;

    SDL_Window* window = static_cast<SDL_Window*>(window_);
    SDL_Renderer* nativeRenderer = renderer_->GetNativeRenderer();

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, nativeRenderer)) {
        statusMessage_ = "ImGui SDL3 platform backend initialization failed.";
        return false;
    }

    if (!ImGui_ImplSDLRenderer3_Init(nativeRenderer)) {
        statusMessage_ = "ImGui SDL renderer backend initialization failed.";
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    imguiInitialized_ = true;
    return true;
}

void Application::ShutdownImGui() noexcept {
    if (!imguiInitialized_) {
        return;
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    imguiInitialized_ = false;
}

int Application::Run() {
    if (!Initialize()) {
        return 1;
    }

    while (running_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                RequestExit();
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                RequestExit();
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_O) {
                OpenLoadFbxDialog();
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            yawDegrees_ += io.MouseDelta.x * 0.4f;
            pitchDegrees_ += io.MouseDelta.y * 0.4f;
        }

        UpdateGui();

        renderer_->BeginFrame();
        if (loadedModel_.IsValid()) {
            renderer_->RenderModelWireframe(loadedModel_, yawDegrees_, pitchDegrees_, rollDegrees_, cameraDistance_);
        }
        ImGui::Render();
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_->GetNativeRenderer());
        renderer_->EndFrame();

        ++frameCounter_;
    }

    Shutdown();
    return 0;
}

void Application::UpdateGui() {
    DrawShortcutOverlay();

    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 420.0f), ImGuiCond_Always);
    ImGui::Begin("Model Viewer");
    ImGui::Text("Renderer: %s", renderer_ ? renderer_->GetName() : "None");

    if (ImGui::Button("Load FBX")) {
        OpenLoadFbxDialog();
    }

    ImGui::Separator();
    ImGui::SliderFloat("Yaw", &yawDegrees_, -180.0f, 180.0f);
    ImGui::SliderFloat("Pitch", &pitchDegrees_, -89.0f, 89.0f);
    ImGui::SliderFloat("Roll", &rollDegrees_, -180.0f, 180.0f);
    ImGui::SliderFloat("Camera Distance", &cameraDistance_, 1.5f, 12.0f);

    if (ImGui::Button("Reset Rotation")) {
        yawDegrees_ = 0.0f;
        pitchDegrees_ = 0.0f;
        rollDegrees_ = 0.0f;
    }

    if (loadedModel_.IsValid()) {
        ImGui::Text("Vertices: %d", static_cast<int>(loadedModel_.positions.size()));
        ImGui::Text("Triangles: %d", static_cast<int>(loadedModel_.indices.size() / 3));
        ImGui::TextWrapped("Source: %s", loadedModel_.sourcePath.c_str());
    } else {
        ImGui::TextUnformatted("No model loaded.");
    }

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", statusMessage_.c_str());
    ImGui::TextUnformatted("Drag with left mouse button in empty viewport area to rotate.");
    ImGui::TextUnformatted("Shortcut: press O to open the FBX file dialog.");
    ImGui::End();
}

void Application::DrawShortcutOverlay() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    constexpr ImGuiWindowFlags overlayFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    ImGui::Begin("##ShortcutOverlay", nullptr, overlayFlags);
    ImGui::TextUnformatted("Press O to load FBX");
    ImGui::End();
}

void Application::OpenLoadFbxDialog() {
    const nfdu8filteritem_t filters[] = {
        {"FBX Models", "fbx"},
    };

    nfdu8char_t* selectedPath = nullptr;
    const nfdresult_t dialogResult = NFD_OpenDialogU8(&selectedPath, filters, 1, nullptr);

    if (dialogResult == NFD_OKAY && selectedPath) {
        std::string errorMessage;
        ModelData model;
        if (FbxLoader::LoadModel(selectedPath, model, errorMessage)) {
            loadedModel_ = std::move(model);
            statusMessage_ = "Loaded model successfully.";
            yawDegrees_ = 0.0f;
            pitchDegrees_ = 0.0f;
            rollDegrees_ = 0.0f;
        } else {
            statusMessage_ = "FBX load failed: " + errorMessage;
        }
        NFD_FreePathU8(selectedPath);
    } else if (dialogResult == NFD_CANCEL) {
        statusMessage_ = "File open canceled.";
    } else if (dialogResult == NFD_ERROR) {
        const char* errorText = NFD_GetError();
        statusMessage_ = std::string("File dialog failed: ") + (errorText ? errorText : "Unknown error");
    }
}

void Application::Shutdown() noexcept {
    ShutdownImGui();

    if (renderer_) {
        renderer_->Shutdown();
        renderer_.reset();
    }

    if (window_) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }

    if (nfdInitialized_) {
        NFD_Quit();
        nfdInitialized_ = false;
    }

    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }
}

void Application::RequestExit() noexcept {
    running_ = false;
}

bool Application::IsRunning() const noexcept {
    return running_;
}
}
