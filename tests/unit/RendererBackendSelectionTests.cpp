#include <iostream>
#include <optional>
#include <vector>

#include "Engine/RendererBackendSelection.hpp"

namespace {
int RunRendererBackendSelectionTests() {
    int failureCount = 0;

    if (engine::ParseRendererBackend("").has_value()) {
        std::cerr << "Expected empty backend value to parse as nullopt.\n";
        ++failureCount;
    }

    const auto parsedDx12 = engine::ParseRendererBackend("DX12");
    if (!parsedDx12.has_value() || parsedDx12.value() != engine::RendererBackend::Dx12) {
        std::cerr << "Expected DX12 parsing to succeed case-insensitively.\n";
        ++failureCount;
    }

    const auto parsedVulkan = engine::ParseRendererBackend("VuLkAn");
    if (!parsedVulkan.has_value() || parsedVulkan.value() != engine::RendererBackend::Vulkan) {
        std::cerr << "Expected Vulkan parsing to succeed case-insensitively.\n";
        ++failureCount;
    }

    const auto parsedSoftware = engine::ParseRendererBackend("software");
    if (!parsedSoftware.has_value() || parsedSoftware.value() != engine::RendererBackend::Software) {
        std::cerr << "Expected software parsing to succeed.\n";
        ++failureCount;
    }

    if (engine::ParseRendererBackend("metal").has_value()) {
        std::cerr << "Expected unknown backend to parse as nullopt.\n";
        ++failureCount;
    }

    const std::vector<engine::RendererBackend> autoOrder = engine::BuildRendererAttemptOrder(std::nullopt);
    const std::vector<engine::RendererBackend> expectedAutoOrder = {
        engine::RendererBackend::Dx12,
        engine::RendererBackend::Vulkan,
        engine::RendererBackend::Software,
    };
    if (autoOrder != expectedAutoOrder) {
        std::cerr << "Expected automatic backend order to be dx12 -> vulkan -> software.\n";
        ++failureCount;
    }

    const std::vector<engine::RendererBackend> forcedOrder =
        engine::BuildRendererAttemptOrder(engine::RendererBackend::Vulkan);
    if (forcedOrder.size() != 1 || forcedOrder.front() != engine::RendererBackend::Vulkan) {
        std::cerr << "Expected forced backend order to contain only requested backend.\n";
        ++failureCount;
    }

    if (std::string(engine::RendererBackendName(engine::RendererBackend::Dx12)) != "dx12") {
        std::cerr << "Expected dx12 backend name string.\n";
        ++failureCount;
    }

    if (std::string(engine::RendererBackendName(engine::RendererBackend::Vulkan)) != "vulkan") {
        std::cerr << "Expected vulkan backend name string.\n";
        ++failureCount;
    }

    if (std::string(engine::RendererBackendName(engine::RendererBackend::Software)) != "software") {
        std::cerr << "Expected software backend name string.\n";
        ++failureCount;
    }

    return failureCount;
}
}

int main() {
    const int failures = RunRendererBackendSelectionTests();
    if (failures > 0) {
        std::cerr << "RendererBackendSelection unit tests failed with " << failures << " failure(s).\n";
        return 1;
    }

    std::cout << "RendererBackendSelection unit tests passed.\n";
    return 0;
}
