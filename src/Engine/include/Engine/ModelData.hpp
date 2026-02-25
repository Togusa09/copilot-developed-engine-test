#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace engine {
struct ModelData {
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> indices;
    std::string sourcePath;

    [[nodiscard]] bool IsValid() const noexcept {
        return !positions.empty() && !indices.empty();
    }
};
}
