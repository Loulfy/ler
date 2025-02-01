//
// Created by loulfy on 03/03/2024.
//

#include "mesh_list.hpp"

namespace ler::render
{
void RenderMeshList::installStaticScene(const rhi::DevicePtr& device,
                                        const flatbuffers::Vector<const scene::Instance*>& instanceEntries)
{
    m_drawInstances.reserve(instanceEntries.size());
    for (const auto inst : instanceEntries)
    {
        auto& drawInst = m_drawInstances.emplace_back();
        drawInst.model = glm::make_mat4(inst->transform()->data());
        drawInst.skinIndex = inst->skin_id();
        drawInst.meshIndex = inst->mesh_id();
    }

    rhi::BufferDesc bufDesc;
    bufDesc.debugName = "instanceBuffer";
    bufDesc.stride = sizeof(DrawInstance);
    bufDesc.sizeInBytes = sizeof(DrawInstance) * m_drawInstances.size();
    m_instanceBuffer = device->createBuffer(bufDesc);

    rhi::BufferPtr staging = device->createBuffer(bufDesc.sizeInBytes, true);
    staging->uploadFromMemory(m_drawInstances.data(), bufDesc.sizeInBytes);
    rhi::CommandPtr command = device->createCommand(rhi::QueueType::Graphics);
    command->copyBuffer(staging, m_instanceBuffer, bufDesc.sizeInBytes, 0);
    device->submitOneShot(command);
}

const rhi::BufferPtr& RenderMeshList::getInstanceBuffer() const
{
    return m_instanceBuffer;
}

const DrawInstance& RenderMeshList::getInstance(uint32_t id) const
{
    return m_drawInstances[id];
}

const IndexedMesh& RenderMeshList::getMesh(uint32_t id) const
{
    return m_meshes->getMesh(id);
}

uint32_t RenderMeshList::getInstanceCount() const
{
    return static_cast<uint32_t>(m_drawInstances.size());
}

void RenderMeshList::bindVertices(rhi::CommandPtr& cmd)
{
    m_meshes->bind(cmd, false);
}
} // namespace ler::render