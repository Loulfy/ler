//
// Created by Loulfy on 09/03/2025.
//

#pragma once

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/ProgressHandler.hpp>
#include <assimp/scene.h>
#include <common.h>
#include <log/log.hpp>

#include <filesystem>
#include <flatbuffers/minireflect.h>
namespace fs = std::filesystem;
#include <bitset>
#include <fstream>

#include "archive_generated.h"

namespace ler::pak
{
struct PackedTextureMetadata
{
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t mipLevels = 0;
    uint64_t totalBytes = 0;
    std::string filename;
    CMP_FORMAT format;
    fs::path gpuFile;
};

class PakPacker
{
  public:
    enum MaterialMap
    {
        MT_Normal = 1 << 0,
        MT_Diffuse = 1 << 1,
        MT_Emissive = 1 << 2,
        MT_Occlusion = 1 << 3,
        MT_Metallic = 1 << 4,
        MT_Roughness = 1 << 5,
        MT_Specular = 1 << 6,
        MT_BaseColor = MT_Diffuse,
        MT_SpecularGlossiness = MT_Specular,
        MT_RoughnessMetallic = MT_Metallic | MT_Roughness,
        MT_OcclusionRoughnessMetallic = MT_Occlusion | MT_RoughnessMetallic,
    };
    struct MaterialFormatMapping
    {
        aiTextureType textureType;
        CMP_FORMAT format;
        MaterialMap matType;
        uint8_t index;
    };

    explicit PakPacker(const fs::path& path);
    void setParentDir(const fs::path& root);
    static std::string_view toString(unsigned long matType);
    void processMaterial(const aiScene* aiScene);
    void processSceneNode(aiNode* aiNode, aiMesh** meshes);
    void processTextures(const aiScene* aiScene, bool cook = true);
    void processMeshes(const aiScene* aiScene);
    void finish();

  private:
    fs::path m_root;
    std::ofstream m_outFile;
    flatbuffers::FlatBufferBuilder m_builder;
    std::vector<flatbuffers::Offset<PakEntry>> m_entries;
    std::unordered_map<std::string, PackedTextureMetadata> m_textureMap;
    std::vector<Material> m_materialVector;
    std::vector<Instance> m_instanceVector;
    std::array<std::vector<aiVector3D>, 4> m_vertexBuffers;
    std::vector<uint32_t> m_indexBuffer;
    std::vector<Mesh> m_meshVector;

    uint32_t m_meshCount = 0;
    uint32_t m_materialCount = 0;

    static TextureFormat convertCMPFormat(CMP_FORMAT fmt);
    void exportTexture(const aiScene* aiScene, const fs::path& path, PackedTextureMetadata& metadata,
                       bool skipCompress);
    void concatenateFilesWithAlignment(std::ofstream& outFile, flatbuffers::FlatBufferBuilder& builder,
                                       std::vector<flatbuffers::Offset<PakEntry>>& entries);
};

void exportMaterial(const aiScene* aiScene, std::unordered_map<std::string, CMP_FORMAT>& outTextureMap);
} // namespace ler::pak