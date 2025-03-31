//
// Created Loulfy on 13/03/2025.
//

#include "importer.hpp"

#include <meshoptimizer.h>

namespace ler::pak
{
static Vec3 toVec3(const aiVector3D& vec)
{
    return { vec.x, vec.y, vec.z };
}

void PakPacker::processMeshes(const aiScene* aiScene)
{
    uint64_t indexCount = 0;
    uint64_t vertexCount = 0;
    std::vector<uint32_t> indices;
    std::vector<aiVector3D> vertices;

    m_meshCount += aiScene->mNumMeshes;

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
        m_meshVector.emplace_back(uint32_t(indices.size()), uint32_t(m_indexBuffer.size()),
                                  uint32_t(m_vertexBuffers[0].size()), uint32_t(vertices.size()), toVec3(min),
                                  toVec3(max));

        m_indexBuffer.insert(m_indexBuffer.end(), indices.begin(), indices.end());

        for (size_t n = 0; n < streams.size(); ++n)
        {
            const meshopt_Stream& s = streams[n];
            meshopt_remapVertexBuffer(vertices.data(), s.data, mesh->mNumVertices, s.stride, remapTable.data());

            /*meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), &vertices.front().x,
                                     vertices.size(), sizeof(aiVector3D), 1.01f);*/

            m_vertexBuffers[n].insert(m_vertexBuffers[n].end(), vertices.begin(), vertices.end());
        }
    }
}

void PakPacker::processSceneNode(aiNode* aiNode, aiMesh** meshes)
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
        m_instanceVector.emplace_back(meshId + m_meshCount, skinId + m_materialCount, sp);
    }

    for (size_t i = 0; i < aiNode->mNumChildren; ++i)
        processSceneNode(aiNode->mChildren[i], meshes);
}
} // namespace ler::pak