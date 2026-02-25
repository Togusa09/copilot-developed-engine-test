#include <filesystem>
#include <iostream>
#include <string>

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

        if (loadedModel.sourcePath.empty()) {
            std::cerr << "Expected loaded model source path to be set for asset: " << knownAsset.string() << "\n";
            ++failureCount;
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
