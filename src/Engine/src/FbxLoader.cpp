#include "Engine/FbxLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <unordered_map>

#include <assimp/material.h>
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
    outModel.texCoords.clear();
    outModel.indices.clear();
    outModel.primaryTexturePath.clear();
    outModel.texturePaths.clear();
    outModel.submeshes.clear();
    outModel.animations.clear();
    outModel.sourcePath = filePath.string();

    std::unordered_map<std::string, std::int32_t> textureLookup;
    auto registerTexturePath = [&](const std::string& texturePath) -> std::int32_t {
        if (texturePath.empty()) {
            return -1;
        }

        const auto existing = textureLookup.find(texturePath);
        if (existing != textureLookup.end()) {
            return existing->second;
        }

        const std::int32_t textureIndex = static_cast<std::int32_t>(outModel.texturePaths.size());
        outModel.texturePaths.push_back(texturePath);
        textureLookup.emplace(texturePath, textureIndex);
        return textureIndex;
    };

    auto resolveMaterialTexturePath = [&](unsigned int materialIndex, aiTextureType textureType) -> std::string {
        if (materialIndex >= scene->mNumMaterials) {
            return {};
        }

        const aiMaterial* material = scene->mMaterials[materialIndex];
        if (!material) {
            return {};
        }

        const unsigned int textureCount = material->GetTextureCount(textureType);
        for (unsigned int textureIndex = 0; textureIndex < textureCount; ++textureIndex) {
            aiString texturePath;
            if (material->GetTexture(textureType, textureIndex, &texturePath) != aiReturn_SUCCESS || texturePath.length == 0) {
                continue;
            }

            std::filesystem::path candidate(texturePath.C_Str());
            if (candidate.is_relative()) {
                candidate = filePath.parent_path() / candidate;
            }

            const std::filesystem::path normalized = candidate.lexically_normal();
            if (std::filesystem::exists(normalized)) {
                return normalized.string();
            }

            if (textureIndex == 0) {
                return normalized.string();
            }
        }

        return {};
    };

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
            continue;
        }

        const std::uint32_t baseVertex = static_cast<std::uint32_t>(outModel.positions.size());

        for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
            const aiVector3D& vertex = mesh->mVertices[vertexIndex];
            outModel.positions.emplace_back(vertex.x, vertex.y, vertex.z);

            if (mesh->HasTextureCoords(0)) {
                const aiVector3D& uv = mesh->mTextureCoords[0][vertexIndex];
                outModel.texCoords.emplace_back(uv.x, uv.y);
            } else {
                outModel.texCoords.emplace_back(0.0f, 0.0f);
            }
        }

        const std::uint32_t indexStart = static_cast<std::uint32_t>(outModel.indices.size());
        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            const aiFace& face = mesh->mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }

            outModel.indices.push_back(baseVertex + face.mIndices[0]);
            outModel.indices.push_back(baseVertex + face.mIndices[1]);
            outModel.indices.push_back(baseVertex + face.mIndices[2]);
        }

        const std::uint32_t indexCount = static_cast<std::uint32_t>(outModel.indices.size()) - indexStart;
        if (indexCount == 0) {
            continue;
        }

        auto resolveFirstMaterialTexturePath = [&](unsigned int materialIndex, std::initializer_list<aiTextureType> textureTypes) -> std::string {
            for (const aiTextureType textureType : textureTypes) {
                std::string texturePath = resolveMaterialTexturePath(materialIndex, textureType);
                if (!texturePath.empty()) {
                    return texturePath;
                }
            }
            return {};
        };

        const std::string texturePath = resolveFirstMaterialTexturePath(
            mesh->mMaterialIndex,
            {aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR});
        const std::string opacityTexturePath = resolveMaterialTexturePath(mesh->mMaterialIndex, aiTextureType_OPACITY);
        const std::string normalTexturePath = resolveFirstMaterialTexturePath(
            mesh->mMaterialIndex,
            {aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS, aiTextureType_HEIGHT});
        const std::string emissiveTexturePath = resolveMaterialTexturePath(mesh->mMaterialIndex, aiTextureType_EMISSIVE);
        const std::string specularTexturePath = resolveMaterialTexturePath(mesh->mMaterialIndex, aiTextureType_SPECULAR);
        if (outModel.primaryTexturePath.empty() && !texturePath.empty()) {
            outModel.primaryTexturePath = texturePath;
        }

        float materialOpacity = 1.0f;
        const bool materialHasOpacityTexture = !opacityTexturePath.empty();
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            if (material) {
                const bool hasOpacityProperty = material->Get(AI_MATKEY_OPACITY, materialOpacity) == aiReturn_SUCCESS;

                float transparencyFactor = 0.0f;
                const bool hasTransparencyFactor =
                    material->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparencyFactor) == aiReturn_SUCCESS;

                aiColor3D transparentColor(0.0f, 0.0f, 0.0f);
                const bool hasTransparentColor =
                    material->Get(AI_MATKEY_COLOR_TRANSPARENT, transparentColor) == aiReturn_SUCCESS;

                if (hasTransparencyFactor) {
                    const float opacityFromTransparency = 1.0f - std::clamp(transparencyFactor, 0.0f, 1.0f);
                    if (!hasOpacityProperty || (materialOpacity <= 0.001f && opacityFromTransparency > 0.001f)) {
                        materialOpacity = opacityFromTransparency;
                    }
                } else if (hasTransparentColor) {
                    const float transparencyFromColor = std::clamp(
                        std::max({transparentColor.r, transparentColor.g, transparentColor.b}),
                        0.0f,
                        1.0f);
                    const float opacityFromColor = 1.0f - transparencyFromColor;
                    if (!hasOpacityProperty || (materialOpacity <= 0.001f && opacityFromColor > 0.001f)) {
                        materialOpacity = opacityFromColor;
                    }
                }

                if (materialHasOpacityTexture && materialOpacity <= 0.001f) {
                    materialOpacity = 1.0f;
                }
            }
        }

        materialOpacity = std::clamp(materialOpacity, 0.0f, 1.0f);
        const bool alphaCutoutEnabled = materialHasOpacityTexture;
        const float alphaCutoff = alphaCutoutEnabled ? 0.35f : 0.0f;
        const bool isTransparent = !alphaCutoutEnabled && materialOpacity < 0.999f;
        const bool opacityTextureInverted = materialHasOpacityTexture;

        const std::int32_t textureIndex = registerTexturePath(texturePath);
        const std::int32_t opacityTextureIndex = registerTexturePath(opacityTexturePath);
        const std::int32_t normalTextureIndex = registerTexturePath(normalTexturePath);
        const std::int32_t emissiveTextureIndex = registerTexturePath(emissiveTexturePath);
        const std::int32_t specularTextureIndex = registerTexturePath(specularTexturePath);
        outModel.submeshes.push_back(ModelSubmesh{
            indexStart,
            indexCount,
            textureIndex,
            opacityTextureIndex,
            normalTextureIndex,
            emissiveTextureIndex,
            specularTextureIndex,
            materialOpacity,
            alphaCutoff,
            isTransparent,
            alphaCutoutEnabled,
            opacityTextureInverted});
    }

    if (outModel.submeshes.empty() && !outModel.indices.empty()) {
        const std::int32_t fallbackTextureIndex = registerTexturePath(outModel.primaryTexturePath);
        outModel.submeshes.push_back(ModelSubmesh{
            0,
            static_cast<std::uint32_t>(outModel.indices.size()),
            fallbackTextureIndex,
            -1,
            -1,
            -1,
            -1,
            1.0f,
            0.0f,
            false,
            false,
            false});
    }

    for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
        const aiAnimation* animation = scene->mAnimations[animationIndex];
        if (!animation) {
            continue;
        }

        const double ticksPerSecond = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 25.0;
        const double durationSeconds = animation->mDuration > 0.0 ? (animation->mDuration / ticksPerSecond) : 0.0;

        AnimationClip clip;
        clip.name = animation->mName.length > 0 ? animation->mName.C_Str() : ("Animation " + std::to_string(animationIndex + 1));
        clip.durationSeconds = static_cast<float>(durationSeconds);
        clip.ticksPerSecond = static_cast<float>(ticksPerSecond);
        outModel.animations.push_back(std::move(clip));
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
