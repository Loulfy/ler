//
// Created by Loulfy on 09/03/2025.
//

#include "importer.hpp"

#include <assimp/GltfMaterial.h>
#include <xxhash.h>

namespace ler::pak
{
// clang-format off
static constexpr std::initializer_list<PakPacker::MaterialFormatMapping> c_FormatMappings = {
    { aiTextureType_BASE_COLOR,        CMP_FORMAT_BC7, PakPacker::MT_BaseColor, 1 },
    { aiTextureType_DIFFUSE,           CMP_FORMAT_BC7, PakPacker::MT_Diffuse  , 1 },
    { aiTextureType_METALNESS,         CMP_FORMAT_BC1, PakPacker::MT_Metallic , 4 },
    { aiTextureType_DIFFUSE_ROUGHNESS, CMP_FORMAT_BC1, PakPacker::MT_Roughness, 4 },
    { aiTextureType_AMBIENT_OCCLUSION, CMP_FORMAT_BC4, PakPacker::MT_Occlusion, 3 },
    { aiTextureType_LIGHTMAP,          CMP_FORMAT_BC4, PakPacker::MT_Occlusion, 3 },
    { aiTextureType_SPECULAR,          CMP_FORMAT_BC4, PakPacker::MT_Specular , 4 },
    { aiTextureType_EMISSIVE,          CMP_FORMAT_BC1, PakPacker::MT_Emissive , 2 },
    { aiTextureType_NORMALS,           CMP_FORMAT_BC1, PakPacker::MT_Normal   , 0 }
};
// clang-format on

std::string_view PakPacker::toString(unsigned long matType)
{
    switch (matType)
    {
    case MT_OcclusionRoughnessMetallic:
        return "OcclusionRoughnessMetallic";
    case MT_RoughnessMetallic:
        return "RoughnessMetallic";
    case MT_Occlusion:
        return "AmbientOcclusion";
    case MT_BaseColor:
        return "BaseColor";
    case MT_Specular:
        return "Specular";
    case MT_Roughness:
        return "Roughness";
    case MT_Metallic:
        return "Metallic";
    case MT_Emissive:
        return "Emissive";
    case MT_Normal:
        return "Normal";
    default:
        return "Undefined";
    }
}

std::string createTextureStem(const aiScene* aiScene, const aiString& filename)
{
    fs::path pathOut;
    const fs::path pathIn = filename.C_Str();
    const aiTexture* em = aiScene->GetEmbeddedTexture(filename.C_Str());
    if (em == nullptr)
    {
        pathOut /= pathIn.filename();
    }
    else
    {
        if (em->mFilename.length == 0)
        {
            uint64_t hash = XXH3_64bits(filename.C_Str(), filename.length);
            pathOut /= std::to_string(hash);
        }
        else
            pathOut /= em->mFilename.C_Str();
    }

    return pathOut.stem().string();
}

void PakPacker::processMaterial(const aiScene* aiScene)
{
    std::string_view t = "white";
    uint64_t placeHolder = XXH3_64bits(t.data(), t.size());

    aiString filename;
    aiString aiAlphaMode;
    aiColor4D baseColor;
    aiColor4D emissiveColor;
    float alphaCutOff = 0.f;
    aiShadingMode aiShadingMode;
    float metallicFactor, roughnessFactor, opacity;
    std::unordered_map<fs::path, std::bitset<16>> textures;

    m_materialCount += aiScene->mNumMaterials;

    for (size_t i = 0; i < aiScene->mNumMaterials; ++i)
    {
        aiMaterial* material = aiScene->mMaterials[i];
        material->Get(AI_MATKEY_SHADING_MODEL, aiShadingMode);
        material->Get(AI_MATKEY_GLTF_ALPHAMODE, aiAlphaMode);
        material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutOff);
        material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor);
        material->Get(AI_MATKEY_OPACITY, opacity);
        material->Get(AI_MATKEY_NAME, filename);

        ShadingMode shadingMode;
        AlphaMode alphaMode = AlphaMode_Mask;
        if (aiShadingMode == aiShadingMode_PBR_BRDF)
        {
            float factor;
            aiReturn ret = material->Get(AI_MATKEY_GLOSSINESS_FACTOR, factor);
            if (ret == aiReturn_SUCCESS)
            {
                material->Get(AI_MATKEY_SPECULAR_FACTOR, metallicFactor);
                material->Get(AI_MATKEY_GLOSSINESS_FACTOR, roughnessFactor);
                shadingMode = ShadingMode::ShadingMode_PbrSpecularGlosiness;
            }
            else
            {
                material->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor);
                material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);
                shadingMode = ShadingMode::ShadingMode_PbrRoughnessMetallic;
            }

            material->Get(AI_MATKEY_BASE_COLOR, baseColor);
            std::string_view alpha = aiAlphaMode.C_Str();
            if (alpha == "OPAQUE")
                alphaMode = AlphaMode_Opaque;
            else if (alpha == "MASK")
                alphaMode = AlphaMode_Mask;
            else if (alpha == "BLEND")
                alphaMode = AlphaMode_Blend;
        }
        else
        {
            material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
            shadingMode = ShadingMode::ShadingMode_Phong;
            alphaCutOff = 0.5f;
        }

        log::info("{}: {}, alphaMode={}, baseColor=[{}; {}; {}; {}]", filename.C_Str(),
                  EnumNameShadingMode(shadingMode), EnumNameAlphaMode(alphaMode), baseColor.r, baseColor.g, baseColor.b,
                  baseColor.a);

        std::array<uint64_t, 6> mapColor = {};
        mapColor.fill(placeHolder);
        for (const PakPacker::MaterialFormatMapping& mapping : c_FormatMappings)
        {
            if (material->GetTextureCount(mapping.textureType) > 0)
            {
                material->GetTexture(mapping.textureType, 0, &filename);
                textures[filename.C_Str()] |= mapping.matType;
                if (!m_textureMap.contains(filename.C_Str()))
                {
                    PackedTextureMetadata metadata;
                    metadata.format = mapping.format;
                    metadata.filename = createTextureStem(aiScene, filename);
                    m_textureMap.emplace(filename.C_Str(), metadata);
                }

                const std::string& stem = m_textureMap[filename.C_Str()].filename;
                mapColor[mapping.index] = XXH3_64bits(stem.c_str(), stem.size());
            }
        }

        for (auto& e : textures)
            log::info(" - {} -> {}", e.first.string(), PakPacker::toString(e.second.to_ulong()));

        m_materialVector.emplace_back(shadingMode, alphaMode, alphaCutOff, Vec3(baseColor.r, baseColor.g, baseColor.b),
                                      Vec3(emissiveColor.r, emissiveColor.g, emissiveColor.b), metallicFactor,
                                      roughnessFactor, opacity, mapColor);
        textures.clear();
    }
}
} // namespace ler::pak