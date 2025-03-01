//
// Created by loulfy on 22/02/2024.
//

#include "scene.hpp"
#include "render/mesh.hpp"

#include <compressonator.h>
#include <meshoptimizer.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <fstream>
namespace ler::scene
{
// clang-format off
static constexpr std::initializer_list<TexturePacker::MaterialFormatMapping> c_FormatMappings = {
    { aiTextureType_BASE_COLOR,        CMP_FORMAT_BC1, TexturePacker::MT_BaseColor, 1 },
    { aiTextureType_DIFFUSE,           CMP_FORMAT_BC1, TexturePacker::MT_Diffuse  , 1 },
    { aiTextureType_METALNESS,         CMP_FORMAT_BC1, TexturePacker::MT_Metallic , 4 },
    { aiTextureType_DIFFUSE_ROUGHNESS, CMP_FORMAT_BC1, TexturePacker::MT_Roughness, 4 },
    { aiTextureType_AMBIENT_OCCLUSION, CMP_FORMAT_BC4, TexturePacker::MT_Occlusion, 3 },
    { aiTextureType_LIGHTMAP,          CMP_FORMAT_BC4, TexturePacker::MT_Occlusion, 3 },
    { aiTextureType_SPECULAR,          CMP_FORMAT_BC4, TexturePacker::MT_Specular , 4 },
    { aiTextureType_EMISSIVE,          CMP_FORMAT_BC1, TexturePacker::MT_Emissive , 2 },
    { aiTextureType_NORMALS,           CMP_FORMAT_BC1, TexturePacker::MT_Normal   , 0 }
};
// clang-format on

std::string_view TexturePacker::toString(unsigned long matType)
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

static void processSceneNode(std::vector<Instance>& instances, aiNode* aiNode, aiMesh** meshes)
{
    for (size_t i = 0; i < aiNode->mNumMeshes; ++i)
    {
        uint32_t meshId = aiNode->mMeshes[i];
        uint32_t skinId = meshes[meshId]->mMaterialIndex;

        aiMatrix4x4 model = aiNode->mTransformation;
        auto* currentParent = aiNode->mParent;
        while (currentParent)
        {
            model = currentParent->mTransformation * model;
            currentParent = currentParent->mParent;
        }

        model.Transpose();

        auto sp = flatbuffers::span<const float, 16>(model[0], 16);
        instances.emplace_back(meshId, skinId, sp);
    }

    for (size_t i = 0; i < aiNode->mNumChildren; ++i)
        processSceneNode(instances, aiNode->mChildren[i], meshes);
}

static Vec3 toVec3(const aiVector3D& vec)
{
    return { vec.x, vec.y, vec.z };
}

CMP_BOOL CompressionCallback(CMP_FLOAT fProgress, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2)
{
    UNREFERENCED_PARAMETER(pUser1);
    UNREFERENCED_PARAMETER(pUser2);

    // std::printf("\rCompression progress = %3.0f", fProgress);

    return false;
}

void CMP_LoadTexture(const aiTexture* texture, MipSet* mipSet)
{
    const auto* buffer = reinterpret_cast<const unsigned char*>(texture->pcData);

    int width, height, componentCount;
    stbi_uc* image = stbi_load_from_memory(buffer, static_cast<int>(texture->mWidth), &width, &height, &componentCount,
                                           STBI_rgb_alpha);

    CMP_CMIPS CMips;

    if (!CMips.AllocateMipSet(mipSet, CF_8bit, TDT_ARGB, TT_2D, width, height, 1))
        return;

    if (!CMips.AllocateMipLevelData(CMips.GetMipLevel(mipSet, 0), width, height, CF_8bit, TDT_ARGB))
        return;

    mipSet->m_nMipLevels = 1;
    mipSet->m_format = CMP_FORMAT_RGBA_8888;

    CMP_BYTE* pData = CMips.GetMipLevel(mipSet, 0)->m_pbData;

    // RGBA : 8888 = 4 bytes
    CMP_DWORD dwPitch = (4 * mipSet->m_nWidth);
    CMP_DWORD dwSize = dwPitch * mipSet->m_nHeight;

    memcpy(pData, image, dwSize);

    mipSet->pData = pData;
    mipSet->dwDataSize = dwSize;

    stbi_image_free(image);
}

TexturePacker::TexturePacker(const fs::path& name, fs::path root) : m_root(std::move(root))
{
    m_pathOut = sys::getHomeDir();
    m_pathOut /= sys::PACKED_DIR;
    m_pathOut /= name;
    fs::create_directories(m_pathOut);
    m_root.make_preferred();
}

uint32_t TexturePacker::addTexture(const fs::path& path, const MaterialFormatMapping& matMapping)
{
    uint32_t id;
    if(m_textureMap.contains(path))
    {
        id = m_textureMap.at(path);
    }
    else
    {
        id = m_textureList.size();
        m_textureList.emplace_back(path, matMapping.format);
        m_textureMap.insert_or_assign(path, id);
    }

    TextureEntry& entry = m_textureList[id];
    entry.flags |= matMapping.matType;
    return id;
}

std::vector<std::string> TexturePacker::process(const aiScene* aiScene, flatbuffers::FlatBufferBuilder& builder)
{
    std::vector<std::string> textureVector;
    for (TextureEntry& entry : m_textureList)
    {
        // log::info("{} -> [{}]", path.string(), fmt::join(e.second, ","));
        std::string ext = entry.path.extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);
        if (ext == ".dds")
            continue;
        entry.filename = exportTexture(aiScene, entry.path, entry.format);
        textureVector.emplace_back(entry.filename.string());
    }
    return textureVector;
}

fs::path TexturePacker::exportTexture(const aiScene* aiScene, const fs::path& path, CMP_FORMAT compressedFormat)
{
    CMP_InitFramework();

    MipSet MipSetIn = {};

    fs::path pathOut = m_pathOut;
    fs::path pathIn = path;
    const aiTexture* em = aiScene->GetEmbeddedTexture(pathIn.string().c_str());
    if (em == nullptr)
    {
        pathIn = m_root / pathIn;
        CMP_LoadTexture(pathIn.string().c_str(), &MipSetIn);
        pathOut /= path.filename();
    }
    else
    {
        CMP_LoadTexture(em, &MipSetIn);
        pathOut /= em->mFilename.length == 0 ? path.stem() : em->mFilename.C_Str();
    }

    pathOut.replace_extension(".dds");
    pathOut = pathOut.make_preferred();
    std::string filename = pathOut.filename().string();
    log::info("[Packer] Processing texture: {}", filename);

    if (MipSetIn.m_nMipLevels <= 1)
    {
        CMP_INT requestLevel = 16;
        CMP_INT nMinSize = CMP_CalcMinMipSize(MipSetIn.m_nHeight, MipSetIn.m_nWidth, requestLevel);
        CMP_GenerateMIPLevels(&MipSetIn, nMinSize);
    }

    //==========================
    // Set Compression Options
    //==========================
    KernelOptions kernel_options = {};

    kernel_options.format = compressedFormat;
    kernel_options.fquality = 1;
    kernel_options.threads = 0;

    //=====================================================
    // example of using BC1 encoder options
    // kernel_options.bc15 is valid for BC1 to BC5 formats
    //=====================================================
    {
        // Enable punch through alpha setting
        kernel_options.bc15.useAlphaThreshold = true;
        kernel_options.bc15.alphaThreshold = 128;

        // Enable setting channel weights
        kernel_options.bc15.useChannelWeights = true;
        kernel_options.bc15.channelWeights[0] = 0.3086f;
        kernel_options.bc15.channelWeights[1] = 0.6094f;
        kernel_options.bc15.channelWeights[2] = 0.0820f;
    }

    //--------------------------------------------------------------
    // Setup a results buffer for the processed file,
    // the content will be set after the source texture is processed
    // in the call to CMP_ConvertMipTexture()
    //--------------------------------------------------------------
    CMP_MipSet MipSetCmp;
    memset(&MipSetCmp, 0, sizeof(CMP_MipSet));

    //===============================================
    // Compress the texture using Compressonator Lib
    //===============================================
    CMP_ERROR cmp_status = CMP_ProcessTexture(&MipSetIn, &MipSetCmp, kernel_options, CompressionCallback);
    if (cmp_status != CMP_OK)
    {
        CMP_FreeMipSet(&MipSetIn);
        std::printf("Compression returned an error %d\n", cmp_status);
        return {};
    }

    //----------------------------------------------------------------
    // Save the result into a DDS file
    //----------------------------------------------------------------
    std::string file = pathOut.string();
    cmp_status = CMP_SaveTexture(file.c_str(), &MipSetCmp);
    CMP_FreeMipSet(&MipSetIn);
    CMP_FreeMipSet(&MipSetCmp);

    if (cmp_status != CMP_OK)
    {
        std::printf("Error %d: Saving processed file %s!\n", cmp_status, pathOut.c_str());
        return {};
    }

    return pathOut;
}

std::vector<Material> exportMaterial(const aiScene* aiScene,
                                                          flatbuffers::FlatBufferBuilder& builder,
                                                          TexturePacker& packer)
{
    aiString filename;
    aiColor3D baseColor;
    aiShadingMode aiShadingMode;
    std::unordered_map<fs::path, std::bitset<16>> textures;
    flatbuffers::Offset<flatbuffers::String> n, d, r, o;
    std::vector<Material> material_vector;

    for (size_t i = 0; i < aiScene->mNumMaterials; ++i)
    {
        aiMaterial* material = aiScene->mMaterials[i];
        material->Get(AI_MATKEY_SHADING_MODEL, aiShadingMode);
        material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
        material->Get(AI_MATKEY_NAME, filename);

        ShadingMode shadingMode;
        if(aiShadingMode == aiShadingMode_PBR_BRDF)
        {
            float factor;
            aiReturn ret = material->Get(AI_MATKEY_GLOSSINESS_FACTOR, factor);
            if(ret == aiReturn_SUCCESS)
                shadingMode = ShadingMode::ShadingMode_PbrSpecularGlosiness;
            else
                shadingMode = ShadingMode::ShadingMode_PbrRoughnessMetallic;
        }
        else
        {
            shadingMode = ShadingMode::ShadingMode_Phong;
        }

        log::info("{}: {}", filename.C_Str(), EnumNameShadingMode(shadingMode));

        // materials[i].color = glm::vec3(baseColor.r, baseColor.g, baseColor.b);

        std::array<uint16_t,6> mapColor = {};
        for (const TexturePacker::MaterialFormatMapping& mapping : c_FormatMappings)
        {
            if (material->GetTextureCount(mapping.textureType) > 0)
            {
                material->GetTexture(mapping.textureType, 0, &filename);
                mapColor[mapping.index] = packer.addTexture(filename.C_Str(), mapping);
                textures[filename.C_Str()] |= mapping.matType;
            }
        }

        for(auto& e : textures)
            log::info(" - {} -> {}", e.first.string(), TexturePacker::toString(e.second.to_ulong()));

        // n = builder.CreateString(p.string());
        material_vector.emplace_back(shadingMode, mapColor);
        textures.clear();
    }

    return material_vector;
}

void SceneImporter::prepare(const fs::path& path, const std::string& output)
{
    using namespace render;
    Assimp::Importer importer;
    unsigned int postProcess = aiProcessPreset_TargetRealtime_Fast;
    postProcess |= aiProcess_ConvertToLeftHanded;
    postProcess |= aiProcess_GenBoundingBoxes;
    postProcess |= aiProcess_GenUVCoords;
    const aiScene* aiScene = importer.ReadFile(path.string(), postProcess);

    if (aiScene == nullptr || aiScene->mNumMeshes == 0)
    {
        log::error(importer.GetErrorString());
        return;
    }

    TexturePacker texturePacker(path.stem(), path.parent_path());
    flatbuffers::FlatBufferBuilder builder(32768);
    std::vector<Material> material_vector = exportMaterial(aiScene, builder, texturePacker);
    exportMaterial(aiScene, builder, texturePacker);
    auto texture_vector = texturePacker.process(aiScene, builder);

    uint64_t indexCount = 0;
    uint64_t vertexCount = 0;
    std::vector<uint32_t> indices;
    std::vector<aiVector3D> vertices;
    std::vector<meshopt_Meshlet> meshlets;
    std::vector<Meshlet> meshlet_vector; // aiScene->mNumMeshes
    std::vector<Mesh> mesh_vector;

    std::array<std::vector<aiVector3D>, 4> vertexBuffers;
    for (auto& b : vertexBuffers)
        b.reserve(sys::C16Mio);

    std::vector<uint32_t> indexBuffer;
    indexBuffer.reserve(sys::C16Mio);

    for (size_t i = 0; i < aiScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = aiScene->mMeshes[i];
        vertexCount = mesh->mNumVertices;
        indexCount = mesh->mNumFaces * 3;

        indices.clear();
        indices.reserve(mesh->mNumFaces * 3);
        for (size_t n = 0; n < mesh->mNumFaces; ++n)
            indices.insert(indices.end(), mesh->mFaces[n].mIndices, mesh->mFaces[n].mIndices + 3);

        std::array<meshopt_Stream, 4> streams = { { { mesh->mVertices, sizeof(aiVector3D), sizeof(aiVector3D) },
                                                    { mesh->mTextureCoords[0], sizeof(aiVector3D), sizeof(aiVector3D) },
                                                    { mesh->mNormals, sizeof(aiVector3D), sizeof(aiVector3D) },
                                                    { mesh->mTangents, sizeof(aiVector3D), sizeof(aiVector3D) } } };

        std::vector<uint32_t> remapTable(indexCount);
        vertexCount = meshopt_generateVertexRemapMulti(remapTable.data(), indices.data(), indexCount, vertexCount,
                                                       streams.data(), streams.size());

        vertices.resize(vertexCount);
        indices.resize(indexCount);

        meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), remapTable.data());
        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());

        // meshopt_optimizeVertexFetchRemap(indices.data(), indices.data(), indices.size(), vertexCount);

        aiVector3D min = mesh->mAABB.mMin;
        aiVector3D max = mesh->mAABB.mMax;
        mesh_vector.emplace_back(uint32_t(indices.size()), uint32_t(indexBuffer.size()),
                                 uint32_t(vertexBuffers[0].size()), uint32_t(vertices.size()), toVec3(min),
                                 toVec3(max));

        indexBuffer.insert(indexBuffer.end(), indices.begin(), indices.end());

        for (size_t n = 0; n < streams.size(); ++n)
        {
            const meshopt_Stream& s = streams[n];
            meshopt_remapVertexBuffer(vertices.data(), s.data, mesh->mNumVertices, s.stride, remapTable.data());

            /*meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), &vertices.front().x,
                                     vertices.size(), sizeof(aiVector3D), 1.01f);*/

            vertexBuffers[n].insert(vertexBuffers[n].end(), vertices.begin(), vertices.end());
        }

        size_t meshletCount = meshopt_buildMeshletsBound(indices.size(), MeshBuffers::kMaxVerticesPerMeshlet,
                                                         MeshBuffers::kMaxTrianglesPerMeshlet);
        meshlets.resize(meshletCount);

        std::vector<uint32_t> meshletVertices(meshletCount * MeshBuffers::kMaxVerticesPerMeshlet);
        std::vector<uint8_t> meshletTriangles(meshletCount * MeshBuffers::kMaxTrianglesPerMeshlet * 3);

        const aiVector3D* position = vertexBuffers.front().data();
        meshletCount =
            meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(),
                                  indices.size(), &position[0].x, vertices.size(), sizeof(aiVector3D),
                                  MeshBuffers::kMaxVerticesPerMeshlet, MeshBuffers::kMaxTrianglesPerMeshlet, 0.7f);

        meshlets.resize(meshletCount);
        for (const meshopt_Meshlet& rMeshlet : meshlets)
        {
            meshlet_vector.emplace_back(rMeshlet.vertex_count, rMeshlet.vertex_offset, rMeshlet.triangle_count,
                                        rMeshlet.triangle_offset);
        }
    }

    fs::path outputPath = output;
    outputPath.concat(".bin");
    std::ofstream f(sys::ASSETS_DIR / outputPath, std::ios::binary);

    f.write(reinterpret_cast<const char*>(indexBuffer.data()), uint32_t(indexBuffer.size() * sizeof(uint32_t)));
    for (auto& b : vertexBuffers)
        f.write(reinterpret_cast<const char*>(b.data()), uint32_t(b.size() * sizeof(aiVector3D)));
    f.close();

    std::vector<Buffer> buffer_vector;
    uint64_t offset = indexBuffer.size() * sizeof(uint32_t);
    uint64_t length = vertexBuffers.front().size() * sizeof(aiVector3D);
    buffer_vector.emplace_back(offset, 0, BufferType_Index);
    for (uint32_t i = 0; i < 4; ++i)
        buffer_vector.emplace_back(length, offset + length * i, static_cast<scene::BufferType>(i + 1));

    std::vector<Instance> instance_vector;
    processSceneNode(instance_vector, aiScene->mRootNode, aiScene->mMeshes);

    // Write scene
    outputPath.replace_extension(".mesh");
    f.open(sys::ASSETS_DIR / outputPath, std::ios::binary);

    auto instances = builder.CreateVectorOfStructs(instance_vector);
    auto materials = builder.CreateVectorOfStructs(material_vector);
    auto textures = builder.CreateVectorOfStrings(texture_vector);
    auto buffers = builder.CreateVectorOfStructs(buffer_vector);
    auto meshes = builder.CreateVectorOfStructs(mesh_vector);
    auto scene = CreateScene(builder, instances, materials, textures, buffers, meshes);
    FinishSceneBuffer(builder, scene);

    uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    f.write(reinterpret_cast<const char*>(buf), std::streamsize(size));
    f.close();
}

/*
void SceneImporter::prepare(const fs::path& path, const std::string& output)
{
    Assimp::Importer importer;
    unsigned int postProcess = aiProcessPreset_TargetRealtime_Fast;
    postProcess |= aiProcess_ConvertToLeftHanded;
    postProcess |= aiProcess_GenBoundingBoxes;
    const aiScene* aiScene = importer.ReadFile(path.string(), postProcess);

    if (aiScene == nullptr || aiScene->mNumMeshes == 0)
    {
        log::error(importer.GetErrorString());
        return;
    }

    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
    std::vector<Mesh> mesh_vector;
    mesh_vector.reserve(aiScene->mNumMeshes);
    for (size_t i = 0; i < aiScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = aiScene->mMeshes[i];
        aiVector3D min = mesh->mAABB.mMin;
        aiVector3D max = mesh->mAABB.mMax;
        mesh_vector.emplace_back(mesh->mNumFaces * 3, indexCount, vertexCount, mesh->mNumVertices, toVec3(min),
                                 toVec3(max));
        vertexCount += mesh_vector.back().count_vertex();
        indexCount += mesh_vector.back().count_index();
    }

    std::vector<Buffer> buffer_vector;
    uint32_t offset = indexCount * sizeof(uint32_t);
    uint32_t length = vertexCount * sizeof(aiVector3D);
    buffer_vector.emplace_back(offset, 0, BufferType_Index);
    for (size_t i = 0; i < 4; ++i)
        buffer_vector.emplace_back(length, offset + length * i, (scene::BufferType)(i + 1));

    fs::path outputPath = output;
    outputPath.concat(".bin");
    std::ofstream f(sys::ASSETS_DIR / outputPath, std::ios::binary);

    std::vector<Meshlet> meshletInfo;

    // Concat INDEX
    std::vector<uint32_t> indices;
    for (size_t i = 0; i < aiScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = aiScene->mMeshes[i];

        indices.clear();
        indices.reserve(mesh->mNumFaces * 3);
        for (size_t n = 0; n < mesh->mNumFaces; ++n)
            indices.insert(indices.end(), mesh->mFaces[n].mIndices, mesh->mFaces[n].mIndices + 3);

        f.write(reinterpret_cast<const char*>(indices.data()), uint32_t(indices.size() * sizeof(uint32_t)));
    }

    // Concat Position
    std::vector<aiVector3D> vertices;
    for (size_t i = 0; i < aiScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = aiScene->mMeshes[i];

        vertices.clear();
        vertices.reserve(mesh->mNumVertices);

        vertices.insert(vertices.end(), mesh->mVertices, mesh->mVertices + mesh->mNumVertices);
        // f.write(reinterpret_cast<const char*>(vertices.data()), uint32_t(vertices.size() * sizeof(aiVector3D)));

        std::vector<uint32_t> remapTable(indexCount);
        vertexCount = meshopt_generateVertexRemap(remapTable.data(), nullptr, indexCount, vertices.data(),
                                                  vertices.size(), sizeof(aiVector3D));

        vertices.resize(vertexCount);
        indices.resize(indexCount);

        meshopt_remapVertexBuffer(vertices.data(), vertices.data(), indices.size(), sizeof(aiVector3D),
                                  remapTable.data());
        meshopt_remapIndexBuffer(indices.data(), nullptr, indices.size(), remapTable.data());

        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertices.size());
        meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), &vertices.front().x, vertices.size(),
                                 sizeof(aiVector3D), 1.01f);
        meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), vertices.size(),
                                    sizeof(aiVector3D));

        f.write(reinterpret_cast<const char*>(vertices.data()), uint32_t(vertices.size() * sizeof(aiVector3D)));

        size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), render::MeshBuffers::kMaxVerticesPerMeshlet,
                                                        render::MeshBuffers::kMaxTrianglesPerMeshlet);
        std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
        std::vector<uint32_t> meshletVertices(maxMeshlets * render::MeshBuffers::kMaxVerticesPerMeshlet);
        std::vector<uint8_t> meshletTriangles(maxMeshlets * render::MeshBuffers::kMaxTrianglesPerMeshlet * 3);

        // TODO-MILKRU: After per-meshlet frustum/occlusion culling gets implemented, try playing around with
        // cone_weight. You might get better performance.
        size_t meshletCount = meshopt_buildMeshlets(meshlets.data(), meshletVertices.data(), meshletTriangles.data(),
                                                    indices.data(), indices.size(), &vertices[0].x, vertices.size(),
                                                    sizeof(aiVector3D), render::MeshBuffers::kMaxVerticesPerMeshlet,
                                                    render::MeshBuffers::kMaxTrianglesPerMeshlet, 0.7f);
    }

    // Concat Texcoord
    for (size_t i = 0; i < aiScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = aiScene->mMeshes[i];

        vertices.clear();
        vertices.reserve(mesh->mNumVertices);

        vertices.insert(vertices.end(), mesh->mTextureCoords[0], mesh->mTextureCoords[0] + mesh->mNumVertices);
        f.write(reinterpret_cast<const char*>(vertices.data()), uint32_t(vertices.size() * sizeof(aiVector3D)));
    }

    // Concat Normal
    for (size_t i = 0; i < aiScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = aiScene->mMeshes[i];

        vertices.clear();
        vertices.reserve(mesh->mNumVertices);

        vertices.insert(vertices.end(), mesh->mNormals, mesh->mNormals + mesh->mNumVertices);
        f.write(reinterpret_cast<const char*>(vertices.data()), uint32_t(vertices.size() * sizeof(aiVector3D)));
    }

    // Concat Tangent
    for (size_t i = 0; i < aiScene->mNumMeshes; ++i)
    {
        aiMesh* mesh = aiScene->mMeshes[i];

        vertices.clear();
        vertices.reserve(mesh->mNumVertices);

        vertices.insert(vertices.end(), mesh->mTangents, mesh->mTangents + mesh->mNumVertices);
        f.write(reinterpret_cast<const char*>(vertices.data()), uint32_t(vertices.size() * sizeof(aiVector3D)));
    }

    f.close(); // flush BIN

    // Write scene
    outputPath.replace_extension(".inst");
    f.open(sys::ASSETS_DIR / outputPath, std::ios::binary);

    flatbuffers::FlatBufferBuilder builder(32768);
    std::vector<Instance> instance_vector;

    processSceneNode(instance_vector, aiScene->mRootNode, aiScene->mMeshes);

    aiString filename;
    aiColor3D baseColor;
    flatbuffers::Offset<flatbuffers::String> n, d, r, o;
    std::vector<flatbuffers::Offset<Material>> material_vector;
    for (size_t i = 0; i < aiScene->mNumMaterials; ++i)
    {
        aiMaterial* material = aiScene->mMaterials[i];
        material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
        // materials[i].color = glm::vec3(baseColor.r, baseColor.g, baseColor.b);
        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
        {
            material->GetTexture(aiTextureType_DIFFUSE, 0, &filename);
            fs::path p = "sponza";
            p /= filename.C_Str();
            p.replace_extension(".DDS");
            d = builder.CreateString(p.string());
        }
        if (material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0)
        {
            material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &filename);
            fs::path p = "sponza";
            p /= filename.C_Str();
            p.replace_extension(".DDS");
            r = builder.CreateString(p.string());
        }
        if (material->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0)
        {
            material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &filename);
            fs::path p = "sponza";
            p /= filename.C_Str();
            p.replace_extension(".DDS");
            o = builder.CreateString(p.string());
        }
        if (material->GetTextureCount(aiTextureType_NORMALS) > 0)
        {
            material->GetTexture(aiTextureType_NORMALS, 0, &filename);
            fs::path p = "sponza";
            p /= filename.C_Str();
            p.replace_extension(".DDS");
            n = builder.CreateString(p.string());
        }
        material_vector.emplace_back(CreateMaterial(builder, n, d, r, o));
    }

    auto instances = builder.CreateVectorOfStructs(instance_vector);
    auto materials = builder.CreateVector(material_vector);
    auto buffers = builder.CreateVectorOfStructs(buffer_vector);
    auto meshes = builder.CreateVectorOfStructs(mesh_vector);
    auto scene = CreateScene(builder, instances, materials, buffers, meshes);
    FinishSceneBuffer(builder, scene);

    uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    f.write(reinterpret_cast<const char*>(buf), std::streamsize(size));
    f.close();
}*/
} // namespace ler::scene