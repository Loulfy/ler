//
// Created by Loulfy on 06/01/2024.
//

#include "rhi/vulkan.hpp"

namespace ler::rhi::vulkan
{
Storage::Storage(Device* device, std::shared_ptr<coro::thread_pool>& tp) : CommonStorage(device, tp), m_ios(tp)
{
    const VulkanContext& context = device->getContext();
    std::vector<sys::IoService::BufferInfo> buffers;
    for (const BufferPtr& staging : m_stagings)
    {
        const auto* buff = checked_cast<Buffer*>(staging.get());
        auto* data = static_cast<std::byte*>(buff->hostInfo.pMappedData);
        buffers.emplace_back(data, kStagingSize);
    }
    m_ios.registerBuffers(buffers, context.hostBuffer);
}

ReadOnlyFilePtr Storage::openFile(const fs::path& path)
{
    return std::make_shared<ReadOnlyFile>(path);
}

static void queueTexture(const ReadOnlyFilePtr& file, sys::IoService::FileLoadRequest& req)
{
    const fs::path filename(file->getFilename());
    std::string ext = filename.extension().string();
    std::ranges::transform(ext, ext.begin(), ::tolower);

    if (ext == ".dds")
        req.fileLength = img::DdsTexture::kBytesToRead;
    else if (ext == ".ktx" || ext == ".ktx2")
        req.fileLength = img::KtxTexture::kBytesToRead;

    req.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
    req.fileOffset = 0;
    req.buffOffset = 0;
}

coro::task<> Storage::makeSingleTextureTask(coro::latch& latch, BindlessTablePtr table, ReadOnlyFilePtr file)
{
    sys::IoService::FileLoadRequest request;
    queueTexture(file, request);
    const int bufferIndex = co_await acquireStaging();
    request.buffIndex = bufferIndex;
    co_await m_ios.submit(request);

    const img::ITexture* tex = factoryTexture(file, m_ios.getMemPtr(bufferIndex));

    TextureDesc desc = tex->desc();
    const std::span levels = tex->levels();
    const uint64_t head = tex->headOffset();
    desc.debugName = file->getFilename();
    TexturePtr texture = m_device->createTexture(desc);
    ResourceViewPtr view = table->createResourceView(texture);
    log::info("Load texture {:03}: {}", view->getBindlessIndex(), desc.debugName);

    request.fileLength = tex->getDataSize();
    request.fileOffset = head;
    request.buffIndex = bufferIndex;

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    for (const uint32_t mip : std::views::iota(0u, levels.size()))
    {
        const img::ITexture::LevelIndexEntry& level = levels[mip];
        log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

        Subresource sub;
        sub.index = mip;
        sub.offset = level.byteOffset - head;
        sub.width = desc.width >> mip;
        sub.height = desc.height >> mip;

        cmd->copyBufferToTexture(getStaging(bufferIndex), texture, sub, nullptr);
    }

    co_await m_ios.submit(request);

    m_device->submitOneShot(cmd);

    latch.count_down();
    releaseStaging(bufferIndex);

    co_return;
}

coro::task<> Storage::makeMultiTextureTask(coro::latch& latch, BindlessTablePtr table,
                                           std::vector<ReadOnlyFilePtr> files)
{
    std::vector<uint32_t> stagings;

    constexpr uint64_t capacity = kStagingSize;
    uint64_t offset = capacity;
    int bufferId;

    std::vector<sys::IoService::FileLoadRequest> requests(files.size());
    for (int i = 0; i < files.size(); ++i)
    {
        queueTexture(files[i], requests[i]);
        static constexpr uint64_t alignment = 16;
        const uint64_t byteSizes = align(files[i]->sizeInBytes(), alignment);

        if (offset + byteSizes > capacity)
        {
            offset = 0;
            bufferId = co_await acquireStaging();
            stagings.emplace_back(bufferId);
        }

        requests[i].buffOffset = offset;
        requests[i].buffIndex = bufferId;
        requests[i].fileLength = files[i]->sizeInBytes();

        offset += byteSizes;
    }

    co_await m_ios.submit(requests);

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

    {
        std::lock_guard lock = table->lock();
        for (size_t i = 0; i < files.size(); ++i)
        {
            const ReadOnlyFilePtr& file = files[i];
            const sys::IoService::FileLoadRequest& req = requests[i];
            std::byte* data = m_ios.getMemPtr(req.buffIndex);
            const img::ITexture* tex = factoryTexture(file, data + req.buffOffset);
            std::span levels = tex->levels();
            TextureDesc desc = tex->desc();
            desc.debugName = file->getFilename();

            // uint32_t texIndex = table->allocate();
            TexturePtr texture = m_device->createTexture(desc);
            ResourceViewPtr view = table->createResourceView(texture);
            log::info("Load texture {:03}: {}", view->getBindlessIndex(), desc.debugName);
            // table->setResource(texture, texIndex);

            for (const uint32_t mip : std::views::iota(0u, levels.size()))
            {
                const img::ITexture::LevelIndexEntry& level = levels[mip];
                // log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

                Subresource sub;
                sub.index = mip;
                sub.offset = level.byteOffset + req.buffOffset;
                sub.width = desc.width >> mip;
                sub.height = desc.height >> mip;
                cmd->copyBufferToTexture(getStaging(req.buffIndex), texture, sub, nullptr);
            }
        }
    }

    m_device->submitOneShot(cmd);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}

/*
coro::task<> makeMultiTextureTaskDeprecated(coro::latch& latch, TexturePoolPtr texturePool,
                                            const std::vector<ReadOnlyFilePtr> files)
{
    // ***********************************************************
    // V2
    std::vector<uint32_t> stagings;

    constexpr uint32_t capacity = kStagingSize;
    uint32_t offset = capacity;
    int bufferId;

    // std::vector<img::ITexture*> images;
    // images.reserve(files.size());
    std::vector<sys::IoService::FileLoadRequest> requests(files.size());
    for (int i = 0; i < files.size(); ++i)
    {
        queueTexture(files[i], requests[i]);
        const uint32_t byteSizes = align(files[i]->sizeBytes(), 16u);

        if (offset + byteSizes > capacity)
        {
            offset = 0;
            bufferId = acquireStaging();
            stagings.emplace_back(bufferId);
        }

        requests[i].buffOffset = offset;
        requests[i].buffIndex = bufferId;
        requests[i].fileLength = files[i]->sizeBytes();

        offset += byteSizes;
    }

    co_await m_ios.submit(requests);

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& file = files[i];
        const sys::IoService::FileLoadRequest& req = requests[i];
        img::ITexture* tex = nullptr;
        auto* data = static_cast<std::byte*>(m_ios.getMemPtr(req.buffIndex));
        tex->initFromBuffer(data + req.buffOffset);
        std::span levels = tex->levels();
        TextureDesc desc = tex->desc();
        desc.debugName = file->getFilename();

        log::info("Load texture: {}", desc.debugName);
        TexturePtr texture = m_device->createTexture(desc);
        uint32_t texIndex = texturePool->allocate();
        texturePool->setTexture(texture, texIndex);

        for (const size_t mip : std::views::iota(0u, levels.size()))
        {
            const img::ITexture::LevelIndexEntry& level = levels[mip];
            // log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

            Subresource sub;
            sub.index = mip;
            sub.offset = level.byteOffset + req.buffOffset;
            sub.width = desc.width >> mip;
            sub.height = desc.height >> mip;
            cmd->copyBufferToTexture(m_stagings[req.buffIndex], texture, sub, nullptr);
        }
    }

    // ***********************************************************
    // V3
    std::vector<uint32_t> stagings;

    constexpr uint32_t capacity = kStagingSize;
    uint32_t offset = 0;
    int bufferId = acquireStaging();
    stagings.emplace_back(bufferId);

    std::vector<img::ITexture*> images;
    images.reserve(files.size());
    std::vector<sys::IoService::FileLoadRequest> requests(files.size());

    for (int i = 0; i < files.size(); ++i)
    {
        images.emplace_back(queueTexture(files[i], requests[i], m_memory));

        requests[i].buffOffset = offset;
        requests[i].buffIndex = bufferId;
        offset+= requests[i].fileLength;
    }

    co_await m_ios.submit(requests);

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

    offset = capacity;
    for (size_t i = 0; i < files.size(); ++i)
    {
        // Parse Header
        auto& file = files[i];
        const auto& req = requests[i];
        img::ITexture* tex = images[i];
        auto* data = static_cast<std::byte*>(m_ios.getMemPtr(req.buffIndex));
        log::info("Load texture: {}", file->getFilename());
        tex->initFromBuffer(data + req.buffOffset);
        std::span levels = tex->levels();
        TextureDesc desc = tex->desc();
        desc.debugName = file->getFilename();

        log::info("Load texture: {}", desc.debugName);
        TexturePtr texture = m_device->createTexture(desc);
        uint32_t texIndex = texturePool->allocate();
        texturePool->setTexture(texture, texIndex);

        // Read Body
        const uint32_t byteSizes = align(tex->getDataSize(), 16u);

        if (offset + byteSizes > capacity)
        {
            offset = 0;
            bufferId = acquireStaging();
            stagings.emplace_back(bufferId);
        }

        const uint32_t head = tex->headOffset();
        requests[i].buffOffset = offset;
        requests[i].buffIndex = bufferId;
        requests[i].fileLength = byteSizes;
        requests[i].fileOffset = head;

        for (const size_t mip : std::views::iota(0u, levels.size()))
        {
            const img::ITexture::LevelIndexEntry& level = levels[mip];
            // log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

            Subresource sub;
            sub.index = mip;
            sub.offset = level.byteOffset - head + offset;
            sub.width = desc.width >> mip;
            sub.height = desc.height >> mip;
            cmd->copyBufferToTexture(getStaging(bufferId), texture, sub, nullptr);
        }

        offset+= byteSizes;
    }

    co_await m_ios.submit(requests);

    m_device->submitOneShot(cmd);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}*/

coro::task<> Storage::makeBufferTask(coro::latch& latch, ReadOnlyFilePtr file, BufferPtr buffer, uint64_t fileLength,
                                     uint64_t fileOffset)
{
    assert(fileLength < kStagingSize);
    const int bufferId = co_await acquireStaging();
    const BufferPtr staging = getStaging(bufferId);

    sys::IoService::FileLoadRequest request;
    request.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
    request.fileLength = fileLength;
    request.fileOffset = fileOffset;
    request.buffIndex = bufferId;

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    cmd->copyBuffer(staging, buffer, fileLength, 0);

    co_await m_ios.submit(request);

    m_device->submitOneShot(cmd);

    latch.count_down();
    releaseStaging(bufferId);

    co_return;
}
} // namespace ler::rhi::vulkan