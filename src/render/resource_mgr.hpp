//
// Created by Loulfy on 01/03/2025.
//

#pragma once

#include "scene_generated.h"
#include "archive_generated.h"
#include "sys/utils.hpp"
#include "rhi/rhi.hpp"
#include "rhi/storage.hpp"
#include "mesh_list.hpp"
#include "mesh.hpp"

namespace ler::render
{
using PathHashMap = std::unordered_map<fs::path, std::string, sys::PathHash, sys::PathEqual>;
class ResourceManager
{
  public:
    void setup(const rhi::StoragePtr& storage, const rhi::BindlessTablePtr& table);
    bool openArchive(const rhi::DevicePtr& device, const fs::path& path);
    RenderMeshList* createRenderMeshList(const rhi::DevicePtr& device);
    [[nodiscard]] MeshBuffers& getMeshBuffers() { return m_meshBuffers; }

  private:
    rhi::StoragePtr m_storage;
    rhi::BindlessTablePtr m_table;
    std::vector<uint8_t> m_buffer;
    const pak::PakArchive* m_archive = nullptr;
    std::vector<RenderMeshList> m_renderMeshList;
    MeshBuffers m_meshBuffers;

    const pak::PakArchive* readHeader(const fs::path& path);
    static constexpr std::string_view kHeader = "LEPK";
};
} // namespace ler::render