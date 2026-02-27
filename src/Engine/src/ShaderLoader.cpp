#include "Engine/ShaderLoader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <SDL3/SDL.h>

namespace engine::ShaderLoader {
namespace {
std::vector<std::filesystem::path> BuildShaderSearchRoots() {
    std::vector<std::filesystem::path> roots;
    roots.reserve(4);

    if (const char* basePathRaw = SDL_GetBasePath()) {
        const std::filesystem::path basePath(basePathRaw);

        roots.push_back(basePath / "shaders");
        roots.push_back(basePath / ".." / ".." / ".." / ".." / "src" / "Engine" / "shaders");
    }

    roots.push_back(std::filesystem::current_path() / "shaders");
    roots.push_back(std::filesystem::current_path() / "src" / "Engine" / "shaders");

    return roots;
}
}

bool LoadTextFile(std::string_view relativePath, std::string& outSource, std::string& outResolvedPath, std::string& outError) {
    outSource.clear();
    outResolvedPath.clear();

    std::vector<std::filesystem::path> attemptedPaths;
    for (const auto& root : BuildShaderSearchRoots()) {
        const std::filesystem::path candidate = root / relativePath;
        attemptedPaths.push_back(candidate);

        if (!std::filesystem::exists(candidate) || !std::filesystem::is_regular_file(candidate)) {
            continue;
        }

        std::ifstream file(candidate, std::ios::binary);
        if (!file) {
            continue;
        }

        outSource.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!file.good() && !file.eof()) {
            outSource.clear();
            continue;
        }

        outResolvedPath = candidate.generic_string();
        return true;
    }

    std::ostringstream message;
    message << "Unable to locate shader file '" << relativePath << "'. Searched:";
    for (const auto& path : attemptedPaths) {
        message << "\n  - " << path.generic_string();
    }
    outError = message.str();
    return false;
}
}
