//
// Created by loulfy on 01/03/2024.
//

#pragma once

#include "archive_generated.h"
#include "draw.hpp"
#include "rhi/rhi.hpp"
#include "scene_generated.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

namespace ler::render
{
struct IndexedMesh
{
    uint32_t countIndex = 0;
    uint32_t firstIndex = 0;
    uint32_t countVertex = 0;
    int32_t firstVertex = 0;
    uint32_t materialId = 0;
    glm::vec3 bbMin = glm::vec3(0.f);
    glm::vec3 bbMax = glm::vec3(0.f);
    glm::vec4 bounds = glm::vec4(0.f);
    std::string name;
};

class MeshBuffers
{
  public:
    friend class ResourceManager;
    void allocate(const rhi::DevicePtr& device, uint64_t indexSize, uint64_t vertexSize);
    void updateMeshes(const flatbuffers::Vector<const pak::Mesh*>& meshEntries);
    void updateMaterials(const rhi::StoragePtr& storage, const flatbuffers::Vector<const pak::Material*>& materialEntries);
    void flushBuffer(const rhi::DevicePtr& device);
    void bind(const rhi::CommandPtr& cmd, bool prePass) const;
    void bind(rhi::EncodeIndirectIndexedDrawDesc& drawDesc, bool prePass) const;

    static constexpr uint32_t kMaxMesh = 2048;
    static constexpr uint32_t kShaderGroupSizeNV = 32;
    static constexpr uint32_t kMaxVerticesPerMeshlet = 64;
    static constexpr uint32_t kMaxTrianglesPerMeshlet = 124;
    static constexpr uint32_t kNormalTexId = 0;
    static constexpr uint32_t kAlbedoTexId = 1;
    static constexpr uint32_t kEmissiveTexId = 2;
    static constexpr uint32_t kOcclusionTexId = 3;
    static constexpr uint32_t kPbrTexId = 4;

    //[[nodiscard]] const BufferPtr& getVertexBuffer() const;
    //[[nodiscard]] const BufferPtr& getIndexBuffer() const;
    [[nodiscard]] const rhi::BufferPtr& getIndexBuffer() const;
    [[nodiscard]] const rhi::BufferPtr& getMeshBuffer() const;
    [[nodiscard]] const rhi::BufferPtr& getSkinBuffer() const;
    [[nodiscard]] uint32_t getMeshCount() const;

    const IndexedMesh& getMesh(uint32_t id) const;

  private:
    /*
     * 0 : vertex
     * 1 : normal
     * 2 : tangent
     * 3 : texcoord
     * 4 : material
     * 5 : meshlet
     */

    static constexpr std::array<std::string_view, 4> kNames = { "PositionBuffer", "TexcoordBuffer", "NormalBuffer",
                                                                "TangentBuffer" };

    rhi::ReadOnlyFilePtr m_file;
    std::vector<rhi::ReadOnlyFilePtr> m_files;
    rhi::BufferPtr m_indexBuffer;
    std::array<rhi::BufferPtr, 4> m_vertexBuffers;
    std::array<IndexedMesh, kMaxMesh> meshes;

    rhi::BufferPtr m_meshBuffer;
    rhi::BufferPtr m_skinBuffer;
    std::vector<DrawMesh> m_drawMeshes;
    std::vector<DrawSkin> m_drawSkins;

    std::atomic_uint32_t drawCount = 0;
    std::atomic_uint32_t meshCount = 0;
    std::atomic_uint32_t indexCount = 0;
    std::atomic_uint32_t vertexCount = 0;

    rhi::Latch m_latch;
};
} // namespace ler::render