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
    bufDesc.byteSize = sizeof(DrawInstance) * m_drawInstances.size();
    m_instanceBuffer = device->createBuffer(bufDesc);

    rhi::BufferPtr staging = device->createBuffer(bufDesc.byteSize, true);
    staging->uploadFromMemory(m_drawInstances.data(), bufDesc.byteSize);
    rhi::CommandPtr command = device->createCommand(rhi::QueueType::Graphics);
    command->copyBuffer(staging, m_instanceBuffer, bufDesc.byteSize, 0);
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

uint32_t RenderMeshList::getInstanceCount() const
{
    return m_drawInstances.size();
}
} // namespace ler::render