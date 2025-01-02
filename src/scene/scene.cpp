//
// Created by loulfy on 22/02/2024.
//

#include "scene.hpp"
#include "render/mesh.hpp"
#include "scene_generated.h"

#include <meshoptimizer.h>

namespace ler::scene
{
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

std::vector<flatbuffers::Offset<Material>> exportMaterial(const aiScene* aiScene,
                                                          flatbuffers::FlatBufferBuilder& builder)
{
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
    return material_vector;
}

void SceneImporter::prepare(const fs::path& path, const std::string& output)
{
    using namespace render;
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

    flatbuffers::FlatBufferBuilder builder(32768);
    std::vector<flatbuffers::Offset<Material>> material_vector = exportMaterial(aiScene, builder);

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