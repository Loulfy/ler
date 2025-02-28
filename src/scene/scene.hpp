//
// Created by loulfy on 22/02/2024.
//

#pragma once

#include "scene_generated.h"
#include "sys/utils.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <common.h>
#include <set>
#include <unordered_map>
#include <utility>

namespace ler::scene
{
class SceneImporter
{
  public:
    static void prepare(const fs::path& path, const std::string& output);
};

class TexturePacker
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
        TexturePacker::MaterialMap matType;
        uint8_t index;
    };
    struct TextureEntry
    {
        fs::path path;
        CMP_FORMAT format = CMP_FORMAT_Unknown;
        std::bitset<16> flags;
        fs::path filename;
    };

    TexturePacker(const fs::path& name, fs::path root);
    std::vector<std::string> process(const aiScene* aiScene, flatbuffers::FlatBufferBuilder& builder);
    uint32_t addTexture(const fs::path& path, const MaterialFormatMapping& matMapping);

    static std::string_view toString(unsigned long matType);

  private:
    fs::path m_root;
    fs::path m_pathOut;
    std::unordered_map<fs::path, uint32_t> m_textureMap;
    std::vector<TextureEntry> m_textureList;

    fs::path exportTexture(const aiScene* aiScene, const fs::path& path, CMP_FORMAT compressedFormat);
};
} // namespace ler::scene
