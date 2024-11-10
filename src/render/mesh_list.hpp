//
// Created by loulfy on 03/03/2024.
//

#pragma once

#include "mesh.hpp"

namespace ler::render
{
class RenderMeshList
{
  public:
    void installStaticScene(const rhi::DevicePtr& device, const flatbuffers::Vector<const scene::Instance*>& instanceEntries);
    [[nodiscard]] const rhi::BufferPtr& getInstanceBuffer() const;
    [[nodiscard]] const DrawInstance& getInstance(uint32_t id) const;
    [[nodiscard]] uint32_t getInstanceCount() const;
    [[nodiscard]] const IndexedMesh& getMesh(uint32_t id) const;

    void setMeshBuffers(MeshBuffers* meshes) { m_meshes = meshes; }
    [[nodiscard]] MeshBuffers* getMeshBuffers() const { return m_meshes; }
    void bindVertices(rhi::CommandPtr& cmd);

  private:
    MeshBuffers* m_meshes = nullptr;
    rhi::BufferPtr m_instanceBuffer;
    std::vector<DrawInstance> m_drawInstances;
};

struct RenderParams
{
    glm::mat4 proj = glm::mat4(1.f);
    glm::mat4 view = glm::mat4(1.f);
    RenderMeshList* meshList = nullptr;
};
} // namespace ler::render