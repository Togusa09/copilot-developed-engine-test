#include "Engine/FbxLoader.hpp"

#include <algorithm>
#include <limits>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/common.hpp>

namespace engine {
namespace {
void NormalizeModel(ModelData& model) {
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    for (const glm::vec3& point : model.positions) {
        minBounds = glm::min(minBounds, point);
        maxBounds = glm::max(maxBounds, point);
    }

    const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    const glm::vec3 dimensions = maxBounds - minBounds;
    const float maxDimension = std::max({dimensions.x, dimensions.y, dimensions.z});
    const float scale = maxDimension > 0.0001f ? (2.0f / maxDimension) : 1.0f;

    for (glm::vec3& point : model.positions) {
        point = (point - center) * scale;
    }
}
}

bool FbxLoader::LoadModel(const std::filesystem::path& filePath, ModelData& outModel, std::string& outError) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        filePath.string(),
        aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_PreTransformVertices |
            aiProcess_SortByPType |
            aiProcess_ImproveCacheLocality);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !scene->mRootNode) {
        outError = importer.GetErrorString();
        return false;
    }

    outModel.positions.clear();
    outModel.indices.clear();
    outModel.sourcePath = filePath.string();

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
            continue;
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(outModel.positions.size());

        for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
            const aiVector3D& vertex = mesh->mVertices[vertexIndex];
            outModel.positions.emplace_back(vertex.x, vertex.y, vertex.z);
        }

        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            const aiFace& face = mesh->mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }

            outModel.indices.push_back(baseVertex + face.mIndices[0]);
            outModel.indices.push_back(baseVertex + face.mIndices[1]);
            outModel.indices.push_back(baseVertex + face.mIndices[2]);
        }
    }

    if (!outModel.IsValid()) {
        outError = "FBX load succeeded but no triangle geometry was found.";
        return false;
    }

    NormalizeModel(outModel);
    outError.clear();
    return true;
}
}
