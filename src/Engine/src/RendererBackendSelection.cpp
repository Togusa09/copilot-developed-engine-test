#include "Engine/RendererBackendSelection.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace engine {
std::optional<RendererBackend> ParseRendererBackend(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (normalized.empty()) {
        return std::nullopt;
    }

    if (normalized == "dx12") {
        return RendererBackend::Dx12;
    }

    if (normalized == "vulkan") {
        return RendererBackend::Vulkan;
    }

    if (normalized == "software") {
        return RendererBackend::Software;
    }

    return std::nullopt;
}

std::vector<RendererBackend> BuildRendererAttemptOrder(const std::optional<RendererBackend>& requestedBackend) {
    if (requestedBackend.has_value()) {
        return {*requestedBackend};
    }

    return {
        RendererBackend::Dx12,
        RendererBackend::Vulkan,
        RendererBackend::Software,
    };
}

const char* RendererBackendName(RendererBackend backend) noexcept {
    switch (backend) {
    case RendererBackend::Dx12:
        return "dx12";
    case RendererBackend::Vulkan:
        return "vulkan";
    case RendererBackend::Software:
        return "software";
    }

    return "unknown";
}
}
