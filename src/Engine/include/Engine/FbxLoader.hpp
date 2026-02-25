#pragma once

#include <filesystem>
#include <string>

#include "Engine/ModelData.hpp"

namespace engine {
class FbxLoader {
public:
    static bool LoadModel(const std::filesystem::path& filePath, ModelData& outModel, std::string& outError);
};
}
