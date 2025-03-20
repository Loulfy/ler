//
// Created by loria on 12/09/2024.
//

#include "storage.hpp"

namespace ler::rhi
{
CommonStorage::CommonStorage(IDevice* device, std::shared_ptr<coro::thread_pool>& tp)
    : m_device(device), m_scheduler(*tp), m_semaphore(kStagingCount), m_memory(m_buffer.get(), sys::C04Mio)
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
    sys::PathHash hash;
    TextureStreamingBatch batch;
    while (m_dispatcher.dequeue(batch))
    {
        for (auto& e : batch)
            m_resources[hash(e.stem)] = e.view;
    }
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
    if (ext == ".ktx" || ext == ".ktx2")
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
        m_scheduler.spawn(makeSingleTextureTask(latch, table, files.front()));
    else
    {
        std::vector fileList(files.begin(), files.end());
        m_scheduler.spawn(makeMultiTextureTask(latch, table, std::move(fileList)));
    }
}

void CommonStorage::requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer,
                                      uint64_t fileLength, uint64_t fileOffset)
{
    m_scheduler.spawn(makeBufferTask(latch, file, buffer, fileLength, fileOffset));
}

void CommonStorage::requestOpenTexture(coro::latch& latch, BindlessTablePtr& table, const std::span<fs::path>& paths)
{
    int batchCount = 0;
    uint64_t totalByteSizes = 0;
    std::list<std::vector<ReadOnlyFilePtr>> ranges;
    std::vector<ReadOnlyFilePtr> files;
    for (const fs::path& p : paths)
    {
        ReadOnlyFilePtr file = openFile(p);
        static constexpr uint64_t alignment = 16;
        const uint64_t byteSizes = file->sizeInBytes();
        if (totalByteSizes + byteSizes > kStagingSize)
        {
            ranges.emplace_back(std::move(files));
            totalByteSizes = 0;
            ++batchCount;
        }

        totalByteSizes += byteSizes;
        files.emplace_back(file);
    }

    if (!files.empty())
    {
        ranges.emplace_back(std::move(files));
        ++batchCount;
    }

    log::info("[OpenTexture] Requested {} tasks ({} files)", batchCount, paths.size());

    for (std::vector<ReadOnlyFilePtr>& f : ranges)
        m_scheduler.spawn(makeMultiTextureTask(latch, table, std::move(f)));
}

void CommonStorage::requestLoadTexture(coro::latch& latch, BindlessTablePtr& table,
                                       const std::span<TextureStreamingMetadata>& textures)
{
    int batchCount = 0;
    uint64_t totalByteSizes = 0;
    std::list<std::vector<TextureStreamingMetadata>> ranges;
    std::vector<TextureStreamingMetadata> files;
    for (const TextureStreamingMetadata& metadata : textures)
    {
        const uint64_t byteSizes = metadata.byteLength;
        if (totalByteSizes + byteSizes > kStagingSize)
        {
            ranges.emplace_back(std::move(files));
            totalByteSizes = 0;
            ++batchCount;
        }

        totalByteSizes += byteSizes;
        files.emplace_back(metadata);
    }

    if (!textures.empty())
    {
        ranges.emplace_back(std::move(files));
        ++batchCount;
    }

    log::info("[OpenTexture] Requested {} tasks ({} files)", batchCount, textures.size());

    for (std::vector<TextureStreamingMetadata>& f : ranges)
        m_scheduler.spawn(makeMultiTextureTask(latch, table, std::move(f)));
}

void CommonStorage::releaseStaging(uint32_t index)
{
    const std::scoped_lock staging_lock(m_mutex);
    m_bitset.clear(index);
    m_semaphore.release();
}

coro::task<int> CommonStorage::acquireStaging()
{
    // If no staging buffers are available re-schedule the task
    while (!m_semaphore.try_acquire())
        co_await m_scheduler.schedule();
    const std::scoped_lock staging_lock(m_mutex);
    const int idx = m_bitset.findFirst();
    m_bitset.set(idx);
    co_return idx;
}

std::expected<ResourceViewPtr, StorageError> CommonStorage::getResource(uint64_t pathKey)
{
    if (m_resources.contains(pathKey))
        return m_resources.at(pathKey);
    else
        return std::unexpected(StorageError());
}
} // namespace ler::rhi