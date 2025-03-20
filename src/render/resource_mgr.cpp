//
// Created by Loulfy on 01/03/2025.
//

#include "resource_mgr.hpp"

namespace ler::render
{
void ResourceManager::setup(const rhi::StoragePtr& storage, const rhi::BindlessTablePtr& table)
{
    m_storage = storage;
    m_table = table;
}

static constexpr rhi::Format convertFormat(pak::TextureFormat format)
{
    switch (format)
    {
    default:
    case pak::TextureFormat_Bc1:
        return rhi::Format::BC1_UNORM;
    case pak::TextureFormat_Bc2:
        return rhi::Format::BC2_UNORM;
    case pak::TextureFormat_Bc3:
        return rhi::Format::BC3_UNORM;
    case pak::TextureFormat_Bc4:
        return rhi::Format::BC4_UNORM;
    case pak::TextureFormat_Bc5:
        return rhi::Format::BC5_UNORM;
    case pak::TextureFormat_Bc6:
        return rhi::Format::BC6H_SFLOAT;
    case pak::TextureFormat_Bc7:
        return rhi::Format::BC7_UNORM;
    }
}

const pak::PakArchive* ResourceManager::readHeader(const fs::path& path)
{
    std::error_code ec;
    const auto fileSize = static_cast<uint64_t>(fs::file_size(path, ec));
    if (ec.value())
    {
        log::error("File Not Found: {}", path.string());
        return nullptr;
    }

    std::ifstream file(path, std::ios::binary);

    char header[4];
    file.read(header, 4);
    if (!kHeader.compare(header))
        return nullptr;

    int64_t fbSize;
    file.read(reinterpret_cast<char*>(&fbSize), 8);
    if (fbSize > fileSize)
        return nullptr;

    m_buffer.resize(fbSize);
    file.read(reinterpret_cast<char*>(m_buffer.data()), fbSize);
    file.close();

    flatbuffers::Verifier v(m_buffer.data(), fbSize);
    assert(pak::VerifyPakArchiveBuffer(v));
    return pak::GetPakArchive(m_buffer.data());
}

bool ResourceManager::openArchive(const rhi::DevicePtr& device, const fs::path& path)
{
    const pak::PakArchive* archive = readHeader(path);
    if (archive == nullptr)
        return false;

    m_archive = archive;

    rhi::ReadOnlyFilePtr f = m_storage->openFile(path);

    uint64_t indexSize = 0;
    uint64_t vertexSize = 0;
    uint32_t bufferCount = 0;
    uint32_t textureCount = 0;
    for (const pak::PakEntry* entry : *archive->entries())
    {
        if (entry->resource_type() == pak::ResourceType_Buffer)
        {
            bufferCount += 1;
            const pak::Buffer* b = entry->resource_as_Buffer();
            switch (b->type())
            {
            case pak::BufferType_Index:
                indexSize = std::max(indexSize, entry->byte_length());
                break;
            default:
                vertexSize = std::max(vertexSize, entry->byte_length());
                break;
            }
        }
        else if (entry->resource_type() == pak::ResourceType_Texture)
            textureCount += 1;
    }

    m_meshBuffers.allocate(device, indexSize, vertexSize);

    coro::latch l(textureCount + bufferCount);

    std::vector<rhi::TextureStreamingMetadata> resources;
    for (const pak::PakEntry* entry : *archive->entries())
    {
        if (entry->resource_type() == pak::ResourceType_Texture)
        {
            const pak::Texture* t = entry->resource_as_Texture();
            rhi::TextureStreamingMetadata& m = resources.emplace_back();
            m.byteLength = entry->byte_length();
            m.byteOffset = entry->byte_offset();

            m.desc.format = convertFormat(t->format());
            m.desc.debugName = t->filename()->c_str();
            m.desc.mipLevels = t->mip_levels();
            m.desc.height = t->height();
            m.desc.width = t->width();

            m.file = f;
        }
        else if (entry->resource_type() == pak::ResourceType_Buffer)
        {
            const pak::Buffer* b = entry->resource_as_Buffer();
            switch (b->type())
            {
            case pak::BufferType_Index:
                m_storage->requestLoadBuffer(l, f, m_meshBuffers.m_indexBuffer, entry->byte_length(),
                                             entry->byte_offset());
                break;
            case pak::BufferType_Position:
                m_storage->requestLoadBuffer(l, f, m_meshBuffers.m_vertexBuffers[0], entry->byte_length(),
                                             entry->byte_offset());
                break;
            case pak::BufferType_Texcoord:
                m_storage->requestLoadBuffer(l, f, m_meshBuffers.m_vertexBuffers[1], entry->byte_length(),
                                             entry->byte_offset());
                break;
            case pak::BufferType_Normal:
                m_storage->requestLoadBuffer(l, f, m_meshBuffers.m_vertexBuffers[2], entry->byte_length(),
                                             entry->byte_offset());
                break;
            case pak::BufferType_Tangent:
                m_storage->requestLoadBuffer(l, f, m_meshBuffers.m_vertexBuffers[3], entry->byte_length(),
                                             entry->byte_offset());
                break;
            }
        }
    }

    m_storage->requestLoadTexture(l, m_table, resources);
    coro::sync_wait(l);
    m_storage->update();

    m_meshBuffers.updateMeshes(*archive->meshes());
    m_meshBuffers.updateMaterials(m_storage, *archive->materials());
    m_meshBuffers.flushBuffer(device);
    return true;
}

RenderMeshList* ResourceManager::createRenderMeshList(const rhi::DevicePtr& device)
{
    RenderMeshList& meshList = m_renderMeshList.emplace_back();
    meshList.installStaticScene(device, *m_archive->instances());
    meshList.setMeshBuffers(&m_meshBuffers);
    return &meshList;
}
} // namespace ler::render