#include "SdlRendererBase.hpp"

#include <algorithm>
#include <SDL3/SDL.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

namespace engine {
namespace {
struct ProjectedVertex {
    float x;
    float y;
    bool valid;
};

ProjectedVertex ProjectVertex(const glm::vec3& point, const glm::mat4& mvp, int width, int height) {
    const glm::vec4 clip = mvp * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0001f) {
        return {0.0f, 0.0f, false};
    }

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const float screenX = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
    const float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
    return {screenX, screenY, true};
}
}

SdlRendererBase::SdlRendererBase(const char* rendererHint, const char* displayName)
    : rendererHint_(rendererHint), displayName_(displayName), renderer_(nullptr) {}

SdlRendererBase::~SdlRendererBase() {
    Shutdown();
}

bool SdlRendererBase::Initialize(SDL_Window* window, std::string& outError) {
    if (!SDL_SetHint(SDL_HINT_RENDER_DRIVER, rendererHint_)) {
        outError = "Failed to set SDL render driver hint.";
        return false;
    }

    renderer_ = SDL_CreateRenderer(window, nullptr);
    if (!renderer_) {
        outError = SDL_GetError();
        return false;
    }

    return true;
}

void SdlRendererBase::Shutdown() noexcept {
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
}

void SdlRendererBase::BeginFrame() {
    SDL_SetRenderDrawColor(renderer_, 18, 20, 24, 255);
    SDL_RenderClear(renderer_);
}

void SdlRendererBase::EndFrame() {
    SDL_RenderPresent(renderer_);
}

void SdlRendererBase::RenderModelWireframe(const ModelData& model, float yawDegrees, float pitchDegrees, float rollDegrees, float cameraDistance) {
    if (!renderer_ || !model.IsValid()) {
        return;
    }

    int viewportWidth = 0;
    int viewportHeight = 0;
    SDL_GetRenderOutputSize(renderer_, &viewportWidth, &viewportHeight);
    if (viewportWidth <= 1 || viewportHeight <= 1) {
        return;
    }

    const float aspectRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const float clampedDistance = std::clamp(cameraDistance, 1.0f, 20.0f);

    glm::mat4 modelMatrix(1.0f);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(rollDegrees), glm::vec3(0.0f, 0.0f, 1.0f));

    const glm::mat4 viewMatrix = glm::lookAt(
        glm::vec3(0.0f, 0.0f, clampedDistance),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    const glm::mat4 projectionMatrix = glm::perspective(glm::radians(60.0f), aspectRatio, 0.1f, 100.0f);
    const glm::mat4 mvp = projectionMatrix * viewMatrix * modelMatrix;

    std::vector<ProjectedVertex> projected;
    projected.reserve(model.positions.size());
    for (const glm::vec3& point : model.positions) {
        projected.push_back(ProjectVertex(point, mvp, viewportWidth, viewportHeight));
    }

    SDL_SetRenderDrawColor(renderer_, 176, 210, 255, 255);

    const std::size_t indexCount = model.indices.size();
    for (std::size_t index = 0; index + 2 < indexCount; index += 3) {
        const std::uint32_t i0 = model.indices[index];
        const std::uint32_t i1 = model.indices[index + 1];
        const std::uint32_t i2 = model.indices[index + 2];

        if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size()) {
            continue;
        }

        const ProjectedVertex& p0 = projected[i0];
        const ProjectedVertex& p1 = projected[i1];
        const ProjectedVertex& p2 = projected[i2];

        if (!p0.valid || !p1.valid || !p2.valid) {
            continue;
        }

        SDL_RenderLine(renderer_, p0.x, p0.y, p1.x, p1.y);
        SDL_RenderLine(renderer_, p1.x, p1.y, p2.x, p2.y);
        SDL_RenderLine(renderer_, p2.x, p2.y, p0.x, p0.y);
    }
}

SDL_Renderer* SdlRendererBase::GetNativeRenderer() const noexcept {
    return renderer_;
}

const char* SdlRendererBase::GetName() const noexcept {
    return displayName_;
}
}
