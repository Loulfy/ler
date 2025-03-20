//
// Created by loulfy on 01/03/2024.
//

#include "mesh.hpp"

namespace ler::render
{
void MeshBuffers::allocate(const rhi::DevicePtr& device, uint64_t indexSize, uint64_t vertexSize)
{
    rhi::BufferDesc desc;
    desc.isIndexBuffer = true;
    desc.sizeInBytes = indexSize;
    desc.debugName = "IndexBuffer";
    m_indexBuffer = device->createBuffer(desc);

    desc.sizeInBytes = vertexSize;
    desc.isIndexBuffer = false;
    desc.isVertexBuffer = true;
    for (int i = 0; i < m_vertexBuffers.size(); ++i)
    {
        desc.debugName = kNames[i];
        m_vertexBuffers[i] = device->createBuffer(desc);
    }
}

static glm::vec3 toVec3(const pak::Vec3& vec)
{
    return { vec.x(), vec.y(), vec.z() };
}

void MeshBuffers::updateMeshes(const flatbuffers::Vector<const pak::Mesh*>& meshEntries)
{
    meshCount.fetch_add(meshEntries.size());
    for (uint32_t i = 0; i < meshEntries.size(); ++i)
    {
        const pak::Mesh* entry = meshEntries[i];
        meshes[i].countIndex = entry->count_index();
        meshes[i].firstIndex = entry->first_index();
        meshes[i].countVertex = entry->count_vertex();
        meshes[i].firstVertex = entry->first_vertex();
        meshes[i].bbMin = toVec3(entry->bbmin());
        meshes[i].bbMax = toVec3(entry->bbmax());
    }

    m_drawMeshes.resize(getMeshCount());
    for (size_t i = 0; i < m_drawMeshes.size(); ++i)
    {
        const IndexedMesh& mesh = meshes[i];

        DrawMesh& drawMesh = m_drawMeshes[i];

        drawMesh.firstIndex = mesh.firstIndex;
        drawMesh.countIndex = mesh.countIndex;
        drawMesh.firstVertex = mesh.firstVertex;
        drawMesh.countVertex = mesh.countVertex;
        drawMesh.bbMin = glm::vec4(mesh.bbMin, 1.f);
        drawMesh.bbMax = glm::vec4(mesh.bbMax, 1.f);
    }
}

void MeshBuffers::updateMaterials(const rhi::StoragePtr& storage, const flatbuffers::Vector<const pak::Material*>& materialEntries)
{
    m_drawSkins.resize(materialEntries.size());
    for (uint32_t i = 0; i < m_drawSkins.size(); ++i)
    {
        const pak::Material* material = materialEntries[i];
        DrawSkin& skin = m_drawSkins[i];
        skin.alphaMode = material->alpha_mode();
        skin.alphaCutOff = material->alpha_cut_off();
        pak::Vec3 b = material->base_color();
        skin.baseColor = glm::vec3(b.x(), b.y(), b.z());

        skin.textures = glm::uvec4(0.f);
        for(int t = 0; t < 4; ++t)
        {
            uint64_t hash = material->texture()->Get(t);
            std::expected<rhi::ResourceViewPtr, rhi::StorageError> res = storage->getResource(hash);
            if(res.has_value())
                skin.textures[t] = res.value()->getBindlessIndex();
        }
    }
}

void MeshBuffers::flushBuffer(const rhi::DevicePtr& device)
{
    rhi::BufferDesc meshDesc;
    meshDesc.debugName = "MeshBuffer";
    meshDesc.stride = sizeof(DrawMesh);
    meshDesc.sizeInBytes = sizeof(DrawMesh) * m_drawMeshes.size();
    m_meshBuffer = device->createBuffer(meshDesc);

    rhi::BufferDesc skinDesc;
    skinDesc.debugName = "SkinBuffer";
    skinDesc.stride = sizeof(DrawSkin);
    skinDesc.sizeInBytes = sizeof(DrawSkin) * m_drawSkins.size();
    m_skinBuffer = device->createBuffer(skinDesc);

    rhi::CommandPtr command;
    rhi::BufferPtr staging = device->createBuffer(std::max(meshDesc.sizeInBytes, skinDesc.sizeInBytes), true);

    command = device->createCommand(rhi::QueueType::Graphics);
    staging->uploadFromMemory(m_drawMeshes.data(), meshDesc.sizeInBytes);
    command->copyBuffer(staging, m_meshBuffer, meshDesc.sizeInBytes, 0);
    device->submitOneShot(command);

    command = device->createCommand(rhi::QueueType::Graphics);
    staging->uploadFromMemory(m_drawSkins.data(), skinDesc.sizeInBytes);
    command->copyBuffer(staging, m_skinBuffer, skinDesc.sizeInBytes, 0);
    device->submitOneShot(command);
}

void MeshBuffers::bind(const rhi::CommandPtr& cmd, bool prePass) const
{
    const size_t n = prePass ? 1 : m_vertexBuffers.size();
    cmd->bindIndexBuffer(m_indexBuffer);
    for (uint32_t i = 0; i < n; ++i)
        cmd->bindVertexBuffers(i, m_vertexBuffers[i]);
}

void MeshBuffers::bind(rhi::EncodeIndirectIndexedDrawDesc& drawDesc, bool prePass) const
{
    const size_t n = prePass ? 1 : m_vertexBuffers.size();
    drawDesc.indexBuffer = m_indexBuffer;
    for (uint32_t i = 0; i < n; ++i)
        drawDesc.vertexBuffer[i] = m_vertexBuffers[i];
}

const rhi::BufferPtr& MeshBuffers::getIndexBuffer() const
{
    return m_indexBuffer;
}

const rhi::BufferPtr& MeshBuffers::getMeshBuffer() const
{
    return m_meshBuffer;
}

const rhi::BufferPtr& MeshBuffers::getSkinBuffer() const
{
    return m_skinBuffer;
}

uint32_t MeshBuffers::getMeshCount() const
{
    return meshCount.load();
}

const IndexedMesh& MeshBuffers::getMesh(uint32_t id) const
{
    return meshes[id];
}
} // namespace ler::render