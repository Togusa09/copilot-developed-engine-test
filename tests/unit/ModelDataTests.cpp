#include <iostream>

#include "Engine/ModelData.hpp"

namespace {
int RunModelDataIsValidTests() {
    int failureCount = 0;

    engine::ModelData emptyModel;
    if (emptyModel.IsValid()) {
        std::cerr << "Expected empty model to be invalid.\n";
        ++failureCount;
    }
    if (!emptyModel.texCoords.empty()) {
        std::cerr << "Expected empty model texCoords to be empty.\n";
        ++failureCount;
    }
    if (!emptyModel.primaryTexturePath.empty()) {
        std::cerr << "Expected empty model texture path to be empty.\n";
        ++failureCount;
    }
    if (!emptyModel.texturePaths.empty()) {
        std::cerr << "Expected empty model texture path list to be empty.\n";
        ++failureCount;
    }
    if (!emptyModel.submeshes.empty()) {
        std::cerr << "Expected empty model submesh list to be empty.\n";
        ++failureCount;
    }
    if (!emptyModel.animations.empty()) {
        std::cerr << "Expected empty model animations to be empty.\n";
        ++failureCount;
    }

    engine::ModelData onlyPositions;
    onlyPositions.positions.emplace_back(0.0f, 0.0f, 0.0f);
    if (onlyPositions.IsValid()) {
        std::cerr << "Expected model with no indices to be invalid.\n";
        ++failureCount;
    }

    engine::ModelData onlyIndices;
    onlyIndices.indices.push_back(0);
    onlyIndices.indices.push_back(1);
    onlyIndices.indices.push_back(2);
    if (onlyIndices.IsValid()) {
        std::cerr << "Expected model with no positions to be invalid.\n";
        ++failureCount;
    }

    engine::ModelData validModel;
    validModel.positions.emplace_back(0.0f, 0.0f, 0.0f);
    validModel.positions.emplace_back(1.0f, 0.0f, 0.0f);
    validModel.positions.emplace_back(0.0f, 1.0f, 0.0f);
    validModel.texCoords.emplace_back(0.0f, 0.0f);
    validModel.texCoords.emplace_back(1.0f, 0.0f);
    validModel.texCoords.emplace_back(0.0f, 1.0f);
    validModel.indices.push_back(0);
    validModel.indices.push_back(1);
    validModel.indices.push_back(2);
    if (!validModel.IsValid()) {
        std::cerr << "Expected model with positions and indices to be valid.\n";
        ++failureCount;
    }
    if (validModel.texCoords.size() != validModel.positions.size()) {
        std::cerr << "Expected texCoords size to match positions size in valid model fixture.\n";
        ++failureCount;
    }

    return failureCount;
}
}

int main() {
    const int failures = RunModelDataIsValidTests();
    if (failures > 0) {
        std::cerr << "ModelData unit tests failed with " << failures << " failure(s).\n";
        return 1;
    }

    std::cout << "ModelData unit tests passed.\n";
    return 0;
}
