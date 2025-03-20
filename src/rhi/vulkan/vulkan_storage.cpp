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
        buffers.emplace_back(data, static_cast<uint32_t>(kStagingSize));
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

size_t computeDDSPadding(size_t headerSize = 148, size_t existingOffset = 0, size_t alignment = 16)
{
    // Calculate the total offset (header + existing offset)
    size_t totalSize = headerSize + existingOffset;

    // Calculate the remainder when totalSize is divided by alignment
    size_t remainder = totalSize % alignment;

    // Calculate padding needed to make totalSize aligned to the next multiple of alignment
    size_t padding = (alignment - remainder) % alignment;

    return padding;
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
        const uint64_t byteSizes = files[i]->sizeInBytes();

        if (offset + byteSizes > capacity)
        {
            offset = 0;
            bufferId = co_await acquireStaging();
            stagings.emplace_back(bufferId);
        }

        offset += computeDDSPadding(128, offset);
        requests[i].buffOffset = offset;
        requests[i].buffIndex = bufferId;
        requests[i].fileLength = files[i]->sizeInBytes();

        offset += byteSizes;
    }

    co_await m_ios.submit(requests);

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    std::vector<TextureStreaming> result;
    result.reserve(files.size());

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
            result.emplace_back(table->createResourceView(texture), desc.debugName);
            log::info("Load texture {:03}: {}", result.back().view->getBindlessIndex(), desc.debugName);
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
    m_dispatcher.enqueue(result);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}

static constexpr size_t computePitch(Format fmt, size_t width, size_t height)
{
    uint64_t pitch = 0;
    switch (fmt)
    {
    case Format::BC1_UNORM:
    case Format::BC4_UNORM:
    case Format::BC4_SNORM: {
        const uint64_t nbw = std::max<uint64_t>(1u, (uint64_t(width) + 3u) / 4u);
        const uint64_t nbh = std::max<uint64_t>(1u, (uint64_t(height) + 3u) / 4u);
        pitch = nbw * 8u;
    }
    break;
    case Format::BC2_UNORM:
    case Format::BC3_UNORM:
    case Format::BC5_UNORM:
    case Format::BC5_SNORM:
    case Format::BC6H_UFLOAT:
    case Format::BC6H_SFLOAT:
    case Format::BC7_UNORM: {
        const uint64_t nbw = std::max<uint64_t>(1u, (uint64_t(width) + 3u) / 4u);
        const uint64_t nbh = std::max<uint64_t>(1u, (uint64_t(height) + 3u) / 4u);
        pitch = nbw * 16u;
    }
    break;
    default:
        break;
    }

    return static_cast<size_t>(pitch);
}

coro::task<> Storage::makeMultiTextureTask(coro::latch& latch, BindlessTablePtr table,
                                           std::vector<TextureStreamingMetadata> textures)
{
    std::vector<TextureStreaming> result;
    result.reserve(textures.size());

    std::vector<sys::IoService::FileLoadRequest> requests(textures.size());

    uint64_t offset = 0;
    uint64_t offsetSubresource = 0;
    int bufferId = co_await acquireStaging();
    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

    {
        std::lock_guard lock = table->lock();
        for (size_t i = 0; i < textures.size(); ++i)
        {
            const TextureStreamingMetadata& metadata = textures[i];
            const TextureDesc& desc = metadata.desc;
            TexturePtr texture = m_device->createTexture(desc);
            result.emplace_back(table->createResourceView(texture), metadata.desc.debugName);
            log::info("Load texture {:03}: {}", result.back().view->getBindlessIndex(), desc.debugName);

            queueTexture(metadata.file, requests[i]);

            requests[i].buffOffset = offset;
            requests[i].buffIndex = bufferId;
            requests[i].fileLength = metadata.byteLength;
            requests[i].fileOffset = metadata.byteOffset;

            offsetSubresource = offset;

            for (const uint32_t mip : std::views::iota(0u, metadata.desc.mipLevels))
            {
                Subresource sub;
                sub.index = mip;
                sub.offset = offsetSubresource;
                sub.width = desc.width >> mip;
                sub.height = desc.height >> mip;
                uint64_t rowPitch = computePitch(desc.format, sub.width, sub.height);
                sub.rowPitch = rowPitch;
                sub.rowPitch = align(sub.rowPitch, 256u);
                const FormatBlockInfo info = formatToBlockInfo(desc.format);
                sub.rowPitch /= info.blockSizeByte;
                sub.rowPitch *= info.blockWidth;
                cmd->copyBufferToTexture(getStaging(bufferId), texture, sub, nullptr);

                size_t height = std::max<size_t>(1, (sub.height + 3) / 4);
                size_t subResourceSize = align(rowPitch, 256ull) * height;
                subResourceSize = align(subResourceSize, 512ull);
                offsetSubresource += subResourceSize;
            }

            offset += metadata.byteLength;
        }
    }

    co_await m_ios.submit(requests);

    m_device->submitOneShot(cmd);
    auto resCountDown = static_cast<int64_t>(result.size());
    m_dispatcher.enqueue(result);

    latch.count_down(resCountDown);
    releaseStaging(bufferId);

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
    std::vector<uint32_t> stagings;
    uint64_t offset = 0;
    uint64_t remains = fileLength;

    std::vector<sys::IoService::FileLoadRequest> requests;
    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

    while (offset < fileLength)
    {
        int bufferId = co_await acquireStaging();
        stagings.emplace_back(bufferId);

        sys::IoService::FileLoadRequest request;
        request.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
        request.fileLength = std::min(remains, kStagingSize);
        request.fileOffset = fileOffset + offset;
        request.buffIndex = bufferId;
        requests.push_back(request);

        cmd->copyBuffer(getStaging(bufferId), buffer, request.fileLength, offset);

        offset += kStagingSize;
        remains -= kStagingSize;
    }

    co_await m_ios.submit(requests);

    m_device->submitOneShot(cmd);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}
} // namespace ler::rhi::vulkan