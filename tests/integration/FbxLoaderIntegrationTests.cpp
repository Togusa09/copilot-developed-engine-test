#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Engine/FbxLoader.hpp"

namespace {
int RunFbxLoaderIntegrationTests() {
    int failureCount = 0;

    const std::filesystem::path projectRoot = ENGINE_TEST_PROJECT_ROOT;
    const std::filesystem::path wolfDir = projectRoot / "Models" / "Wolf";
    const std::filesystem::path candidateAssets[] = {
        wolfDir / "Wolf.fbx",
        wolfDir / "Wolf_fbx.fbx",
        wolfDir / "Wolf_UDK.fbx",
        wolfDir / "Wolf_UDK_2.fbx"
    };

    bool loadedAtLeastOne = false;
    std::string lastLoadError;

    for (const auto& knownAsset : candidateAssets) {
        engine::ModelData loadedModel;
        std::string loadError;

        if (!engine::FbxLoader::LoadModel(knownAsset, loadedModel, loadError)) {
            lastLoadError = loadError;
            continue;
        }

        loadedAtLeastOne = true;

        if (!loadedModel.IsValid()) {
            std::cerr << "Expected loaded model to be valid for asset: " << knownAsset.string() << "\n";
            ++failureCount;
        }

        if (loadedModel.positions.empty() || loadedModel.indices.empty()) {
            std::cerr << "Expected loaded model to contain positions and indices for asset: " << knownAsset.string() << "\n";
            ++failureCount;
        }

        if (loadedModel.texCoords.size() != loadedModel.positions.size()) {
            std::cerr << "Expected texCoords count to match positions count for asset: " << knownAsset.string() << "\n";
            ++failureCount;
        }

        if (loadedModel.sourcePath.empty()) {
            std::cerr << "Expected loaded model source path to be set for asset: " << knownAsset.string() << "\n";
            ++failureCount;
        }

        if (loadedModel.submeshes.empty()) {
            std::cerr << "Expected loaded model to expose at least one submesh for asset: " << knownAsset.string() << "\n";
            ++failureCount;
        }

        for (const engine::ModelSubmesh& submesh : loadedModel.submeshes) {
            const std::size_t start = static_cast<std::size_t>(submesh.indexStart);
            const std::size_t end = start + static_cast<std::size_t>(submesh.indexCount);
            if (end > loadedModel.indices.size()) {
                std::cerr << "Expected submesh index range to fit within index buffer for asset: " << knownAsset.string() << "\n";
                ++failureCount;
            }

            if (submesh.textureIndex >= 0 && static_cast<std::size_t>(submesh.textureIndex) >= loadedModel.texturePaths.size()) {
                std::cerr << "Expected submesh texture index to reference loaded texture path list for asset: " << knownAsset.string() << "\n";
                ++failureCount;
            }
        }

        for (const engine::AnimationClip& clip : loadedModel.animations) {
            if (clip.name.empty()) {
                std::cerr << "Expected animation clip name to be non-empty for asset: " << knownAsset.string() << "\n";
                ++failureCount;
            }

            if (clip.durationSeconds < 0.0f) {
                std::cerr << "Expected animation clip duration to be non-negative for asset: " << knownAsset.string() << "\n";
                ++failureCount;
            }

            if (clip.ticksPerSecond <= 0.0f) {
                std::cerr << "Expected animation clip ticks-per-second to be positive for asset: " << knownAsset.string() << "\n";
                ++failureCount;
            }
        }

        break;
    }

    if (!loadedAtLeastOne) {
        std::cerr << "Expected at least one known FBX to load, but all candidates failed. Last error: " << lastLoadError << "\n";
        ++failureCount;
    }

    const std::filesystem::path missingAsset =
        projectRoot / "Models" / "Wolf" / "DefinitelyMissing.fbx";
    engine::ModelData missingModel;
    std::string missingError;

    if (engine::FbxLoader::LoadModel(missingAsset, missingModel, missingError)) {
        std::cerr << "Expected missing FBX load to fail.\n";
        ++failureCount;
    } else if (missingError.empty()) {
        std::cerr << "Expected an error message when loading missing FBX.\n";
        ++failureCount;
    }

    return failureCount;
}
}

int main() {
    const int failures = RunFbxLoaderIntegrationTests();
    if (failures > 0) {
        std::cerr << "FbxLoader integration tests failed with " << failures << " failure(s).\n";
        return 1;
    }

    std::cout << "FbxLoader integration tests passed.\n";
    return 0;
}
