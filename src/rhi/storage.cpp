//
// Created by loria on 12/09/2024.
//

#include "storage.hpp"

namespace ler::rhi
{
CommonStorage::CommonStorage(IDevice* device, std::shared_ptr<coro::thread_pool>& tp)
    : m_device(device), m_scheduler(tp), m_semaphore(kStagingCount), m_memory(m_buffer.get(), sys::C04Mio)
{
    for (int i = 0; i < kStagingCount; ++i)
    {
        m_bitset.clear(i);
        BufferPtr staging = device->createHostBuffer(kStagingSize);
        m_stagings.emplace_back(staging);
    }
}

void CommonStorage::update()
{
    m_scheduler.garbage_collect();
    //if (m_scheduler.empty())
      //  m_memory.release();
}

std::vector<ReadOnlyFilePtr> CommonStorage::openFiles(const fs::path& path, const fs::path& ext)
{
    std::vector<ReadOnlyFilePtr> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ext)
            files.emplace_back(openFile(entry.path()));
    }
    return files;
}

img::ITexture* CommonStorage::factoryTexture(const ReadOnlyFilePtr& file, std::byte* metadata)
{
    std::pmr::polymorphic_allocator<> alloc(&m_memory);
    const fs::path filename(file->getFilename());
    std::string ext = filename.extension().string();
    std::ranges::transform(ext, ext.begin(), ::tolower);

    if (ext == ".dds")
    {
        auto* tex = alloc.new_object<img::DdsTexture>();
        memcpy(&tex->ddsMagic, metadata, img::DdsTexture::kBytesToRead);
        tex->init();
        return tex;
    }
    else if (ext == ".ktx" || ext == ".ktx2")
    {
        auto* tex = alloc.new_object<img::KtxTexture>();
        memcpy(tex->identifier, metadata, img::KtxTexture::kBytesToRead);
        tex->init();
        return tex;
    }

    return nullptr;
}

void CommonStorage::requestLoadTexture(coro::latch& latch, BindlessTablePtr& table,
                                       const std::span<ReadOnlyFilePtr>& files)
{
    if (files.size() == 1)
        m_scheduler.start(makeSingleTextureTask(latch, table, files.front()));
    else
    {
        std::vector fileList(files.begin(), files.end());
        m_scheduler.start(makeMultiTextureTask(latch, table, std::move(fileList)));
    }
}

void CommonStorage::requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer,
                                      uint64_t fileLength, uint64_t fileOffset)
{
    m_scheduler.start(makeBufferTask(latch, file, buffer, fileLength, fileOffset));
}

void CommonStorage::releaseStaging(uint32_t index)
{
    m_semaphore.release();
    const std::scoped_lock staging_lock(m_mutex);
    m_bitset.clear(index);
}

int CommonStorage::acquireStaging()
{
    m_semaphore.acquire();
    const std::scoped_lock staging_lock(m_mutex);
    const int idx = m_bitset.findFirst();
    m_bitset.set(idx);
    return idx;
}

} // namespace ler::rhi