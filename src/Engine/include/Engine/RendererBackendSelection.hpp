#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace engine {
enum class RendererBackend {
    Dx12,
    Vulkan,
    Software,
};

[[nodiscard]] std::optional<RendererBackend> ParseRendererBackend(std::string_view value);
[[nodiscard]] std::vector<RendererBackend> BuildRendererAttemptOrder(const std::optional<RendererBackend>& requestedBackend);
[[nodiscard]] const char* RendererBackendName(RendererBackend backend) noexcept;
}
