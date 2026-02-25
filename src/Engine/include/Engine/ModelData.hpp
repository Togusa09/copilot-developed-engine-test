#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace engine {
struct AnimationClip {
    std::string name;
    float durationSeconds;
    float ticksPerSecond;
};

struct ModelSubmesh {
    std::uint32_t indexStart;
    std::uint32_t indexCount;
    std::int32_t textureIndex;
    std::int32_t opacityTextureIndex;
    std::int32_t normalTextureIndex;
    std::int32_t emissiveTextureIndex;
    std::int32_t specularTextureIndex;
    float opacity;
    float alphaCutoff;
    bool isTransparent;
    bool alphaCutoutEnabled;
    bool opacityTextureInverted;
};

struct ModelData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<std::uint32_t> indices;
    std::string primaryTexturePath;
    std::vector<std::string> texturePaths;
    std::vector<ModelSubmesh> submeshes;
    std::vector<AnimationClip> animations;
    std::string sourcePath;

    [[nodiscard]] bool IsValid() const noexcept {
        return !positions.empty() && !indices.empty();
    }
};
}
