#pragma once

#include <string>
#include <string_view>

namespace engine::ShaderLoader {
bool LoadTextFile(std::string_view relativePath, std::string& outSource, std::string& outResolvedPath, std::string& outError);
}
