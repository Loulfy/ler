//
// Created by loulfy on 01/03/2024.
//

#include "mesh.hpp"

namespace ler::render
{
void MeshBuffers::allocate(const rhi::DevicePtr& device, const flatbuffers::Vector<const scene::Buffer*>& bufferEntries)
{
    rhi::StoragePtr storage = device->getStorage();
    m_latch = std::make_shared<coro::latch>(bufferEntries.size());
    m_file = storage->openFile(sys::ASSETS_DIR / "scene.bin");
    for (const scene::Buffer* bufferEntry : bufferEntries)
    {
        rhi::BufferDesc desc;
        rhi::BufferPtr buffer;
        desc.sizeInBytes = bufferEntry->byte_length();
        switch (bufferEntry->type())
        {
        case scene::BufferType_Index:
            desc.isIndexBuffer = true;
            desc.debugName = "IndexBuffer";
            m_indexBuffer = device->createBuffer(desc);
            buffer = m_indexBuffer;
            break;
        case scene::BufferType_Position:
            desc.isVertexBuffer = true;
            desc.debugName = "PositionBuffer";
            m_vertexBuffers[0] = device->createBuffer(desc);
            buffer = m_vertexBuffers[0];
            break;
        case scene::BufferType_Normal:
            desc.isVertexBuffer = true;
            desc.debugName = "NormalBuffer";
            m_vertexBuffers[2] = device->createBuffer(desc);
            buffer = m_vertexBuffers[2];
            break;
        case scene::BufferType_Tangent:
            desc.isVertexBuffer = true;
            desc.debugName = "TangentBuffer";
            m_vertexBuffers[3] = device->createBuffer(desc);
            buffer = m_vertexBuffers[3];
            break;
        case scene::BufferType_Texcoord:
            desc.isVertexBuffer = true;
            desc.debugName = "TexcoordBuffer";
            m_vertexBuffers[1] = device->createBuffer(desc);
            buffer = m_vertexBuffers[1];
            break;
        }

        storage->requestLoadBuffer(*m_latch, m_file, buffer, bufferEntry->byte_length(), bufferEntry->byte_offset());
    }
    coro::sync_wait(*m_latch);
}

void MeshBuffers::allocate(const rhi::DevicePtr& device, rhi::BindlessTablePtr& table, const flatbuffers::Vector<const scene::Material*>& materialEntries)
{
    rhi::StoragePtr storage = device->getStorage();

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

    m_drawSkins.resize(materialEntries.size());
    for (uint32_t i = 0; i < m_drawSkins.size(); ++i)
    {
        const scene::Material* material = materialEntries[i];
        DrawSkin& skin = m_drawSkins[i];

        skin.textures = glm::uvec4(i, 0.f, 0.f, 0.f);
        //log::info(material->diffuse()->c_str());
        //const fs::path path = sys::FileSystemService::Get().resolve(sys::FsTag_Textures, material->diffuse()->string_view());
        //m_files.emplace_back(storage->openFile(path));
    }

    rhi::BufferDesc meshDesc;
    meshDesc.debugName = "meshBuffer";
    meshDesc.stride = sizeof(DrawMesh);
    meshDesc.sizeInBytes = sizeof(DrawMesh) * m_drawMeshes.size();
    m_meshBuffer = device->createBuffer(meshDesc);

    rhi::BufferDesc skinDesc;
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

    //storage->requestLoadTexture(*m_latch, texturePool, m_files);
}

static glm::vec3 toVec3(const scene::Vec3& vec)
{
    return {vec.x(), vec.y(), vec.z()};
}

void MeshBuffers::load(const flatbuffers::Vector<const scene::Mesh*>& meshEntries)
{
    meshCount.fetch_add(meshEntries.size());
    for (uint32_t i = 0; i < meshEntries.size(); ++i)
    {
        const scene::Mesh* entry = meshEntries[i];
        meshes[i].countIndex = entry->count_index();
        meshes[i].firstIndex = entry->first_index();
        meshes[i].countVertex = entry->count_vertex();
        meshes[i].firstVertex = entry->first_vertex();
        meshes[i].bbMin = toVec3(entry->bbmin());
        meshes[i].bbMax = toVec3(entry->bbmax());
    }
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

bool MeshBuffers::isLoaded() const
{
    return m_latch->is_ready();
}
} // namespace ler::render