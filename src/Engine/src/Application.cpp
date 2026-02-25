#include "Engine/Application.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#if defined(_WIN32)
#include <backends/imgui_impl_dx12.h>
#endif
#include <backends/imgui_impl_sdlrenderer3.h>
#include <nfd.h>

#include "Engine/DirectX12Renderer.hpp"
#include "Engine/FbxLoader.hpp"
#include "Engine/NativeDx12Renderer.hpp"
#include "Engine/Renderer.hpp"
#include "Engine/RendererBackendSelection.hpp"
#include "Engine/SoftwareRenderer.hpp"
#include "Engine/VulkanRenderer.hpp"

namespace engine {
namespace {
void LogInfo(std::string_view message) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "%s", message.data());
}

void LogWarning(std::string_view message) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message.data());
}

void LogError(std::string_view message) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.data());
}
}

Application::Application()
    : running_(true),
      frameCounter_(0),
      window_(nullptr),
      renderer_(nullptr),
      yawDegrees_(0.0f),
      pitchDegrees_(0.0f),
      rollDegrees_(0.0f),
      cameraDistance_(4.0f),
            currentAnimationIndex_(0),
            animationTimeSeconds_(0.0f),
            animationSpeed_(1.0f),
            animationPlaying_(true),
            lastFrameCounterTimestamp_(0),
      sdlInitialized_(false),
      nfdInitialized_(false),
        imguiInitialized_(false),
        useNativeDx12ImGui_(false),
                wireOverlayEnabled_(false) {}

Application::~Application() {
    Shutdown();
}

bool Application::Initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        statusMessage_ = SDL_GetError();
        LogError("SDL initialization failed: " + statusMessage_);
        return false;
    }
    sdlInitialized_ = true;
    LogInfo("SDL video subsystem initialized.");

    SDL_Window* window = SDL_CreateWindow("EngineTest - FBX Viewer", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        statusMessage_ = SDL_GetError();
        LogError("Window creation failed: " + statusMessage_);
        return false;
    }
    window_ = window;
    LogInfo("Main window created.");

    if (NFD_Init() != NFD_OKAY) {
        const char* errorText = NFD_GetError();
        statusMessage_ = std::string("NativeFileDialog initialization failed") + (errorText ? std::string(": ") + errorText : ".");
        LogError(statusMessage_);
        return false;
    }
    nfdInitialized_ = true;
    LogInfo("NativeFileDialog initialized.");

    if (!CreateRenderer()) {
        return false;
    }

    if (!InitializeImGui()) {
        return false;
    }

    statusMessage_ = "Ready. Load an FBX file from the UI.";
    LogInfo("Application initialized successfully.");
    return true;
}

bool Application::CreateRenderer() {
    SDL_Window* window = static_cast<SDL_Window*>(window_);
    useNativeDx12ImGui_ = false;

    const char* requestedBackendRaw = SDL_getenv("ENGINE_RENDERER");
    const std::string requestedBackendName = requestedBackendRaw ? requestedBackendRaw : "";
    const std::optional<RendererBackend> requestedBackend = ParseRendererBackend(requestedBackendName);

#if defined(_WIN32)
    const char* nativeDx12Raw = SDL_getenv("ENGINE_NATIVE_DX12");
    const std::string nativeDx12Value = nativeDx12Raw ? nativeDx12Raw : "";
    const bool preferNativeDx12 =
        nativeDx12Value == "1" ||
        nativeDx12Value == "true" ||
        nativeDx12Value == "TRUE" ||
        nativeDx12Value == "on" ||
        nativeDx12Value == "ON";
#else
    const bool preferNativeDx12 = false;
#endif

    if (!requestedBackendName.empty() && !requestedBackend.has_value()) {
        statusMessage_ = "Requested renderer backend is invalid. ENGINE_RENDERER=" + requestedBackendName + ". Use dx12, vulkan, or software.";
        LogError(statusMessage_);
        return false;
    }

    auto tryBackend = [&](RendererBackend backend, std::string& outError) -> bool {
        if (backend == RendererBackend::Dx12) {
#if defined(_WIN32)
            if (preferNativeDx12) {
                auto nativeDx12Renderer = std::make_unique<NativeDx12Renderer>();
                if (nativeDx12Renderer->Initialize(window, outError)) {
                    renderer_ = std::move(nativeDx12Renderer);
                    useNativeDx12ImGui_ = true;
                    LogInfo("Initialized renderer: DirectX 12 Native.");
                    return true;
                }
                useNativeDx12ImGui_ = false;
                return false;
            }
#endif

            auto directXRenderer = std::make_unique<DirectX12Renderer>();
            if (directXRenderer->Initialize(window, outError)) {
                renderer_ = std::move(directXRenderer);
                useNativeDx12ImGui_ = false;
                LogInfo("Initialized renderer: DirectX 12.");
                return true;
            }
            return false;
        }

        if (backend == RendererBackend::Vulkan) {
            auto vulkanRenderer = std::make_unique<VulkanRenderer>();
            if (vulkanRenderer->Initialize(window, outError)) {
                renderer_ = std::move(vulkanRenderer);
                useNativeDx12ImGui_ = false;
                LogInfo("Initialized renderer: Vulkan.");
                return true;
            }
            return false;
        }

        if (backend == RendererBackend::Software) {
            auto softwareRenderer = std::make_unique<SoftwareRenderer>();
            if (softwareRenderer->Initialize(window, outError)) {
                renderer_ = std::move(softwareRenderer);
                useNativeDx12ImGui_ = false;
                LogInfo("Initialized renderer: Software.");
                return true;
            }
            return false;
        }

        outError = "Unknown backend name";
        return false;
    };

    std::string dx12Error;
    std::string vulkanError;
    std::string softwareError;

    const auto backendOrder = BuildRendererAttemptOrder(requestedBackend);
    for (const RendererBackend backend : backendOrder) {
        std::string requestedError;
        if (tryBackend(backend, requestedError)) {
            if (requestedBackend.has_value()) {
                statusMessage_ = "Using renderer backend from ENGINE_RENDERER=" + std::string(RendererBackendName(backend)) + ".";
                LogInfo("Renderer forced by ENGINE_RENDERER=" + std::string(RendererBackendName(backend)) + ".");
            } else if (backend == RendererBackend::Vulkan) {
                statusMessage_ = "DirectX 12 failed, using Vulkan fallback.";
                if (!dx12Error.empty()) {
                    LogWarning("DirectX 12 renderer failed: " + dx12Error);
                }
            } else if (backend == RendererBackend::Software) {
                statusMessage_ = "Hardware backends unavailable, using software fallback renderer.";
                if (!dx12Error.empty()) {
                    LogWarning("DirectX 12 renderer failed: " + dx12Error);
                }
                if (!vulkanError.empty()) {
                    LogWarning("Vulkan renderer failed: " + vulkanError);
                }
            }
            return true;
        }

        if (backend == RendererBackend::Dx12) {
            dx12Error = requestedError;
        } else if (backend == RendererBackend::Vulkan) {
            vulkanError = requestedError;
        } else {
            softwareError = requestedError;
        }
    }

    if (requestedBackend.has_value()) {
        statusMessage_ = "Requested renderer backend failed. ENGINE_RENDERER=" + requestedBackendName + ", error: ";
        if (requestedBackend.value() == RendererBackend::Dx12) {
            statusMessage_ += dx12Error;
        } else if (requestedBackend.value() == RendererBackend::Vulkan) {
            statusMessage_ += vulkanError;
        } else {
            statusMessage_ += softwareError;
        }
        LogError(statusMessage_);
        return false;
    }

    statusMessage_ = "Failed to create renderer. DX12: " + dx12Error + " | Vulkan: " + vulkanError + " | Software: " + softwareError;
    LogError(statusMessage_);
    return false;
}

bool Application::InitializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    SDL_Window* window = static_cast<SDL_Window*>(window_);
    if (!renderer_) {
        statusMessage_ = "ImGui initialization failed: renderer is null.";
        LogError(statusMessage_);
        return false;
    }

    if (useNativeDx12ImGui_) {
#if defined(_WIN32)
        unsigned char* fontPixels = nullptr;
        int fontWidth = 0;
        int fontHeight = 0;
        io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);

        auto* nativeDx12Renderer = dynamic_cast<NativeDx12Renderer*>(renderer_.get());
        if (!nativeDx12Renderer) {
            statusMessage_ = "ImGui initialization failed: native DX12 renderer cast failed.";
            LogError(statusMessage_);
            return false;
        }

        if (!ImGui_ImplSDL3_InitForD3D(window)) {
            statusMessage_ = std::string("ImGui SDL3 D3D platform backend initialization failed: ") + SDL_GetError();
            LogError(statusMessage_);
            return false;
        }

        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "Native DX12 ImGui init. Device=0x%p Queue=0x%p Heap=0x%p",
            static_cast<void*>(nativeDx12Renderer->GetDevice()),
            static_cast<void*>(nativeDx12Renderer->GetCommandQueue()),
            static_cast<void*>(nativeDx12Renderer->GetSrvDescriptorHeap()));

        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = nativeDx12Renderer->GetDevice();
        initInfo.CommandQueue = nativeDx12Renderer->GetCommandQueue();
        initInfo.NumFramesInFlight = nativeDx12Renderer->GetFramesInFlight();
        initInfo.RTVFormat = nativeDx12Renderer->GetRtvFormat();
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.UserData = nativeDx12Renderer;
        initInfo.SrvDescriptorHeap = nativeDx12Renderer->GetSrvDescriptorHeap();
        initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
            auto* nativeRenderer = static_cast<NativeDx12Renderer*>(info->UserData);
            if (!nativeRenderer || !outCpu || !outGpu) {
                return;
            }

            if (!nativeRenderer->AllocateSrvDescriptor(*outCpu, *outGpu)) {
                outCpu->ptr = 0;
                outGpu->ptr = 0;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Native DX12 ImGui SRV allocation failed.");
            }
        };
        initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
            auto* nativeRenderer = static_cast<NativeDx12Renderer*>(info->UserData);
            if (nativeRenderer) {
                nativeRenderer->FreeSrvDescriptor(cpuHandle, gpuHandle);
            }
        };

        if (!ImGui_ImplDX12_Init(&initInfo)) {
            statusMessage_ = "ImGui DX12 renderer backend initialization failed.";
            LogError(statusMessage_);
            ImGui_ImplSDL3_Shutdown();
            return false;
        }

        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;

        imguiInitialized_ = true;
        LogInfo("ImGui initialized successfully (DX12 backend).");
        return true;
#else
        statusMessage_ = "Native DX12 ImGui path is only available on Windows.";
        LogError(statusMessage_);
        return false;
#endif
    }

    SDL_Renderer* nativeRenderer = renderer_->GetNativeRenderer();
    if (!nativeRenderer) {
        statusMessage_ = "ImGui initialization failed: native SDL renderer is null.";
        LogError(statusMessage_);
        return false;
    }

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, nativeRenderer)) {
        statusMessage_ = std::string("ImGui SDL3 platform backend initialization failed: ") + SDL_GetError();
        LogError(statusMessage_);
        return false;
    }

    if (!ImGui_ImplSDLRenderer3_Init(nativeRenderer)) {
        statusMessage_ = std::string("ImGui SDL renderer backend initialization failed: ") + SDL_GetError();
        LogError(statusMessage_);
        ImGui_ImplSDL3_Shutdown();
        return false;
    }

    imguiInitialized_ = true;
    LogInfo("ImGui initialized successfully.");
    return true;
}

void Application::ShutdownImGui() noexcept {
    if (!imguiInitialized_) {
        return;
    }

    if (useNativeDx12ImGui_) {
#if defined(_WIN32)
        if (auto* nativeDx12Renderer = dynamic_cast<NativeDx12Renderer*>(renderer_.get())) {
            nativeDx12Renderer->WaitForGpuIdle();
        }
        ImGui_ImplDX12_Shutdown();
#endif
    } else {
        ImGui_ImplSDLRenderer3_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    imguiInitialized_ = false;
}

int Application::Run() {
    if (!Initialize()) {
        const std::string startupError = statusMessage_.empty() ? "Unknown startup error." : statusMessage_;
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "EngineTest startup failed",
            startupError.c_str(),
            static_cast<SDL_Window*>(window_));
        LogError("Application startup failed: " + startupError);
        Shutdown();
        return 1;
    }

    lastFrameCounterTimestamp_ = SDL_GetTicks();
    bool loggedFirstImGuiFrame = false;
    bool loggedImGuiRenderError = false;
    bool autoFallbackAttempted = false;
    int probableBlankFrameCount = 0;

    const char* requestedBackendRaw = SDL_getenv("ENGINE_RENDERER");
    const bool backendForcedByEnvironment = requestedBackendRaw != nullptr && requestedBackendRaw[0] != '\0';

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

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_LEFTBRACKET) {
                StepAnimationSelection(-1);
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_RIGHTBRACKET) {
                StepAnimationSelection(1);
            }
        }

        const std::uint64_t now = SDL_GetTicks();
        const float deltaSeconds = static_cast<float>(now - lastFrameCounterTimestamp_) / 1000.0f;
        lastFrameCounterTimestamp_ = now;
        UpdateAnimationPlayback(deltaSeconds);

        if (useNativeDx12ImGui_) {
    #if defined(_WIN32)
            ImGui_ImplDX12_NewFrame();
    #endif
        } else {
            ImGui_ImplSDLRenderer3_NewFrame();
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            yawDegrees_ += io.MouseDelta.x * 0.4f;
            pitchDegrees_ += io.MouseDelta.y * 0.4f;
        }
        if (!io.WantCaptureMouse && io.MouseWheel != 0.0f) {
            cameraDistance_ = std::clamp(cameraDistance_ - io.MouseWheel * 0.5f, 1.5f, 12.0f);
        }

        UpdateGui();

        renderer_->BeginFrame();
        if (loadedModel_.IsValid()) {
            renderer_->RenderModelWireframe(loadedModel_, yawDegrees_, pitchDegrees_, rollDegrees_, cameraDistance_, wireOverlayEnabled_);
        }
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (!loggedFirstImGuiFrame && drawData) {
            LogInfo(
                "ImGui first frame draw data: cmd_lists=" + std::to_string(drawData->CmdListsCount) +
                ", total_vertices=" + std::to_string(drawData->TotalVtxCount) +
                ", total_indices=" + std::to_string(drawData->TotalIdxCount));
            loggedFirstImGuiFrame = true;
        }


        if (!useNativeDx12ImGui_ && !backendForcedByEnvironment && !autoFallbackAttempted && renderer_ && std::string(renderer_->GetName()) != "Software") {
            const bool hasUiGeometry = drawData && drawData->CmdListsCount > 0 && drawData->TotalVtxCount > 0;
            if (hasUiGeometry && frameCounter_ < 180) {
                SDL_Rect sampleRect{30, 30, 1, 1};
                SDL_Surface* sampleSurface = SDL_RenderReadPixels(renderer_->GetNativeRenderer(), &sampleRect);
                if (sampleSurface) {
                    std::uint8_t red = 0;
                    std::uint8_t green = 0;
                    std::uint8_t blue = 0;
                    std::uint8_t alpha = 0;
                    if (SDL_ReadSurfacePixel(sampleSurface, 0, 0, &red, &green, &blue, &alpha)) {
                        constexpr int clearRed = 18;
                        constexpr int clearGreen = 20;
                        constexpr int clearBlue = 24;
                        const bool nearClearColor =
                            std::abs(static_cast<int>(red) - clearRed) <= 3 &&
                            std::abs(static_cast<int>(green) - clearGreen) <= 3 &&
                            std::abs(static_cast<int>(blue) - clearBlue) <= 3;
                        probableBlankFrameCount = nearClearColor ? probableBlankFrameCount + 1 : 0;
                    }
                    SDL_DestroySurface(sampleSurface);
                }

                if (probableBlankFrameCount >= 45) {
                    autoFallbackAttempted = true;
                    LogWarning("Detected probable blank output on accelerated backend. Reinitializing renderer with software fallback.");

                    ShutdownImGui();
                    if (renderer_) {
                        renderer_->Shutdown();
                        renderer_.reset();
                    }

                    std::string softwareError;
                    auto softwareRenderer = std::make_unique<SoftwareRenderer>();
                    if (!softwareRenderer->Initialize(static_cast<SDL_Window*>(window_), softwareError)) {
                        statusMessage_ = "Automatic software fallback failed: " + softwareError;
                        LogError(statusMessage_);
                        RequestExit();
                    } else {
                        renderer_ = std::move(softwareRenderer);
                        useNativeDx12ImGui_ = false;
                        if (!InitializeImGui()) {
                            statusMessage_ = "Automatic software fallback failed during ImGui initialization.";
                            LogError(statusMessage_);
                            RequestExit();
                        } else {
                            statusMessage_ = "Detected blank accelerated output. Switched to software renderer.";
                            LogInfo(statusMessage_);
                        }
                    }

                    probableBlankFrameCount = 0;

                    ++frameCounter_;
                    continue;
                }
            }
        }

        if (drawData) {
            SDL_ClearError();
            if (useNativeDx12ImGui_) {
#if defined(_WIN32)
                auto* nativeDx12Renderer = dynamic_cast<NativeDx12Renderer*>(renderer_.get());
                if (nativeDx12Renderer) {
                    ID3D12DescriptorHeap* descriptorHeaps[] = {nativeDx12Renderer->GetSrvDescriptorHeap()};
                    nativeDx12Renderer->GetCommandList()->SetDescriptorHeaps(1, descriptorHeaps);
                    ImGui_ImplDX12_RenderDrawData(drawData, nativeDx12Renderer->GetCommandList());
                }
#endif
            } else {
                ImGui_ImplSDLRenderer3_RenderDrawData(drawData, renderer_->GetNativeRenderer());
            }
            const char* renderError = SDL_GetError();
            if (!loggedImGuiRenderError && renderError && renderError[0] != '\0') {
                LogWarning(std::string("ImGui render reported SDL error on backend '") +
                    (renderer_ ? renderer_->GetName() : "None") +
                    "': " + renderError);
                loggedImGuiRenderError = true;
            }
        }
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
    int renderMode = wireOverlayEnabled_ ? 1 : 0;
    if (ImGui::Combo("Render Mode", &renderMode, "Textured Only\0Textured + Wire Overlay\0")) {
        wireOverlayEnabled_ = (renderMode == 1);
    }

    if (ImGui::Button("Reset Rotation")) {
        yawDegrees_ = 0.0f;
        pitchDegrees_ = 0.0f;
        rollDegrees_ = 0.0f;
    }

    if (loadedModel_.IsValid()) {
        ImGui::Text("Vertices: %d", static_cast<int>(loadedModel_.positions.size()));
        ImGui::Text("Triangles: %d", static_cast<int>(loadedModel_.indices.size() / 3));
        ImGui::Text("Texture: %s", loadedModel_.primaryTexturePath.empty() ? "None" : "Loaded");
        ImGui::Text("Texture Count: %d", static_cast<int>(loadedModel_.texturePaths.size()));
        ImGui::Text("Submeshes: %d", static_cast<int>(loadedModel_.submeshes.size()));
        if (!loadedModel_.primaryTexturePath.empty()) {
            ImGui::TextWrapped("Texture Path: %s", loadedModel_.primaryTexturePath.c_str());
        }

        if (!loadedModel_.texturePaths.empty() && ImGui::TreeNode("Material Texture Paths")) {
            for (std::size_t textureIndex = 0; textureIndex < loadedModel_.texturePaths.size(); ++textureIndex) {
                ImGui::Text("[%d] %s", static_cast<int>(textureIndex), loadedModel_.texturePaths[textureIndex].c_str());
            }
            ImGui::TreePop();
        }

        if (!loadedModel_.submeshes.empty() && ImGui::TreeNode("Submesh Material Bindings")) {
            for (std::size_t submeshIndex = 0; submeshIndex < loadedModel_.submeshes.size(); ++submeshIndex) {
                const ModelSubmesh& submesh = loadedModel_.submeshes[submeshIndex];
                ImGui::Text(
                    "[%d] idx=%u count=%u tex=%d opacityTex=%d normalTex=%d emissiveTex=%d specularTex=%d opacity=%.2f cutoff=%.2f cutout=%s invert=%s transparent=%s",
                    static_cast<int>(submeshIndex),
                    submesh.indexStart,
                    submesh.indexCount,
                    submesh.textureIndex,
                    submesh.opacityTextureIndex,
                    submesh.normalTextureIndex,
                    submesh.emissiveTextureIndex,
                    submesh.specularTextureIndex,
                    submesh.opacity,
                    submesh.alphaCutoff,
                    submesh.alphaCutoutEnabled ? "yes" : "no",
                    submesh.opacityTextureInverted ? "yes" : "no",
                    submesh.isTransparent ? "yes" : "no");
            }
            ImGui::TreePop();
        }

        ImGui::Text("Animations: %d", static_cast<int>(loadedModel_.animations.size()));

        if (!loadedModel_.animations.empty()) {
            if (currentAnimationIndex_ >= loadedModel_.animations.size()) {
                currentAnimationIndex_ = 0;
                animationTimeSeconds_ = 0.0f;
            }

            const AnimationClip& activeClip = loadedModel_.animations[currentAnimationIndex_];
            ImGui::Separator();
            ImGui::Text("Active Animation: %s", activeClip.name.c_str());
            ImGui::Text("Duration: %.2fs", activeClip.durationSeconds);

            if (ImGui::Button("Previous Animation")) {
                StepAnimationSelection(-1);
            }
            ImGui::SameLine();
            if (ImGui::Button("Next Animation")) {
                StepAnimationSelection(1);
            }

            ImGui::Checkbox("Play Animation", &animationPlaying_);
            ImGui::SliderFloat("Animation Speed", &animationSpeed_, 0.1f, 3.0f);

            const float maxTime = activeClip.durationSeconds > 0.0f ? activeClip.durationSeconds : 0.01f;
            ImGui::SliderFloat("Animation Time", &animationTimeSeconds_, 0.0f, maxTime);
        }

        ImGui::TextWrapped("Source: %s", loadedModel_.sourcePath.c_str());
    } else {
        ImGui::TextUnformatted("No model loaded.");
    }

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", statusMessage_.c_str());
    ImGui::TextUnformatted("Drag with left mouse button in empty viewport area to rotate.");
    ImGui::TextUnformatted("Shortcut: press O to open the FBX file dialog.");
    ImGui::TextUnformatted("Animation shortcuts: [ previous, ] next.");
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
            currentAnimationIndex_ = 0;
            animationTimeSeconds_ = 0.0f;
            animationPlaying_ = true;

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loaded model '%s' with %d textures and %d submeshes.",
                loadedModel_.sourcePath.c_str(),
                static_cast<int>(loadedModel_.texturePaths.size()),
                static_cast<int>(loadedModel_.submeshes.size()));

            for (std::size_t textureIndex = 0; textureIndex < loadedModel_.texturePaths.size(); ++textureIndex) {
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Model texture[%d]: %s",
                    static_cast<int>(textureIndex),
                    loadedModel_.texturePaths[textureIndex].c_str());
            }

            for (std::size_t submeshIndex = 0; submeshIndex < loadedModel_.submeshes.size(); ++submeshIndex) {
                const ModelSubmesh& submesh = loadedModel_.submeshes[submeshIndex];
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Submesh[%d]: idxStart=%u idxCount=%u tex=%d opacityTex=%d normalTex=%d emissiveTex=%d specularTex=%d opacity=%.2f cutoff=%.2f cutout=%s invert=%s transparent=%s",
                    static_cast<int>(submeshIndex),
                    submesh.indexStart,
                    submesh.indexCount,
                    submesh.textureIndex,
                    submesh.opacityTextureIndex,
                    submesh.normalTextureIndex,
                    submesh.emissiveTextureIndex,
                    submesh.specularTextureIndex,
                    submesh.opacity,
                    submesh.alphaCutoff,
                    submesh.alphaCutoutEnabled ? "true" : "false",
                    submesh.opacityTextureInverted ? "true" : "false",
                    submesh.isTransparent ? "true" : "false");
            }
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

void Application::UpdateAnimationPlayback(float deltaSeconds) {
    if (!animationPlaying_ || loadedModel_.animations.empty()) {
        return;
    }

    if (currentAnimationIndex_ >= loadedModel_.animations.size()) {
        currentAnimationIndex_ = 0;
    }

    const AnimationClip& activeClip = loadedModel_.animations[currentAnimationIndex_];
    if (activeClip.durationSeconds <= 0.0f) {
        return;
    }

    animationTimeSeconds_ += deltaSeconds * animationSpeed_;
    while (animationTimeSeconds_ > activeClip.durationSeconds) {
        animationTimeSeconds_ -= activeClip.durationSeconds;
    }
    while (animationTimeSeconds_ < 0.0f) {
        animationTimeSeconds_ += activeClip.durationSeconds;
    }
}

void Application::StepAnimationSelection(int direction) {
    if (loadedModel_.animations.empty()) {
        return;
    }

    const std::size_t animationCount = loadedModel_.animations.size();
    if (direction > 0) {
        currentAnimationIndex_ = (currentAnimationIndex_ + 1) % animationCount;
    } else if (direction < 0) {
        currentAnimationIndex_ = currentAnimationIndex_ == 0 ? animationCount - 1 : currentAnimationIndex_ - 1;
    }

    animationTimeSeconds_ = 0.0f;
}
}
