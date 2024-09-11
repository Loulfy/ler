//
// Created by loulfy on 06/01/2024.
//

#include "rhi/vulkan.hpp"
#include "sys/thread.hpp"

namespace ler::rhi::vulkan
{
struct TextureUploadRequest
{
    TexturePtr texture;
    BufferPtr staging;
    Subresource subresource;
    unsigned char* data = nullptr;
};

struct BufferUploadRequest
{
    BufferPtr target;
    BufferPtr source;
    unsigned char* data = nullptr;
};

Storage::~Storage()
{
}

Storage::Storage(const VulkanContext& context, Device* device, std::shared_ptr<coro::thread_pool>& tp)
    : m_device(device), m_context(context), m_memory(m_buffer.get(), sys::C04Mio), m_semaphore(tp->thread_count()),
      m_ios(tp)
{
    m_scheduler = std::make_unique<task_container>(tp);
    std::vector<sys::IoService::BufferInfo> buffers;
    for (int i = 0; i < 8; ++i)
    {
        BufferPtr staging = device->createHostBuffer(kStagingSize);
        auto* buff = checked_cast<Buffer*>(staging.get());
        auto* data = static_cast<std::byte*>(buff->hostInfo.pMappedData);
        buffers.emplace_back(data, kStagingSize);
        putStaging(staging);
    }
    m_ios.registerBuffers(buffers, m_context.hostBuffer);
}

void Storage::update()
{
    m_scheduler->garbage_collect();
}

ReadOnlyFilePtr Storage::openFile(const fs::path& path)
{
    return std::make_shared<ReadOnlyFile>(path);
}

std::vector<ReadOnlyFilePtr> Storage::openFiles(const fs::path& path, const fs::path& ext)
{
    std::vector<ReadOnlyFilePtr> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ext)
            files.emplace_back(openFile(entry.path()));
    }
    return files;
}

img::ITexture* Storage::factoryTexture(const fs::path& filename, std::byte* metadata)
{
    Allocator alloc(&m_memory);
    std::string ext = filename.extension().string();
    std::ranges::transform(ext, ext.begin(), ::tolower);
    if (ext == ".dds")
    {
        auto* tex = alloc.new_object<img::DdsTexture>();
        memcpy(&tex->ddsMagic, metadata, img::DdsTexture::kBytesToRead);
        return tex;
    }
    else if (ext == ".ktx" || ext == ".ktx2")
    {
        auto* tex = alloc.new_object<img::KtxTexture>();
        memcpy(&tex->identifier, metadata, img::KtxTexture::kBytesToRead);
        return tex;
    }

    return nullptr;
}

void Storage::putStaging(const BufferPtr& staging)
{
    const std::scoped_lock staging_lock(m_staging_mutex);
    const size_t idx = m_stagings.size();
    m_staging_bitset.clear(idx);
    m_stagings.push_back(staging);
}

void Storage::releaseStaging(uint32_t index)
{
    m_semaphore.release();
    const std::scoped_lock staging_lock(m_staging_mutex);
    m_staging_bitset.clear(index);
}

int Storage::acquireStaging()
{
    m_semaphore.acquire();
    const std::scoped_lock staging_lock(m_staging_mutex);
    const int idx = m_staging_bitset.findFirst();
    m_staging_bitset.set(idx);
    return idx;
}

static img::ITexture* queueTexture(const ReadOnlyFilePtr& file, sys::IoService::FileLoadRequest& req,
                                   std::pmr::memory_resource& memory)
{
    std::pmr::polymorphic_allocator<> alloc(&memory);
    const fs::path filename(file->getFilename());
    std::string ext = filename.extension().string();
    std::ranges::transform(ext, ext.begin(), ::tolower);

    /*req.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
    req.fileOffset = 0;
    req.buffOffset = 0;

    if (ext == ".dds")
    {
        auto* tex = alloc.new_object<img::DdsTexture>();
        //req.fileLength = img::DdsTexture::kBytesToRead;
        req.fileLength = file->sizeBytes();
        req.buffAddress = &tex->ddsMagic;
        return tex;
    }
    else if (ext == ".ktx" || ext == ".ktx2")
    {
        auto* tex = alloc.new_object<img::KtxTexture>();
        //req.fileLength = img::KtxTexture::kBytesToRead;
        req.fileLength = file->sizeBytes();
        req.buffAddress = &tex->identifier;
        return tex;
    }*/

    /*req.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
    req.fileLength = file->sizeBytes();
    req.fileOffset = 0;
    req.buffOffset = 0;

    if (ext == ".dds")
    {
        return alloc.new_object<img::DdsTexture>();
    }
    if (ext == ".ktx" || ext == ".ktx2")
    {
        return alloc.new_object<img::KtxTexture>();
    }*/

    req.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
    req.fileOffset = 0;
    req.buffOffset = 0;

    if (ext == ".dds")
    {
        auto* tex = alloc.new_object<img::DdsTexture>();
        req.fileLength = img::DdsTexture::kBytesToRead;
        return tex;
    }
    if (ext == ".ktx" || ext == ".ktx2")
    {
        auto* tex = alloc.new_object<img::KtxTexture>();
        req.fileLength = img::KtxTexture::kBytesToRead;
        return tex;
    }

    return nullptr;
}

coro::task<> Storage::makeSingleTextureTask(coro::latch& latch, TexturePoolPtr texturePool,
                                                const ReadOnlyFilePtr file)
{
    TexturePtr texture;
    sys::IoService::FileLoadRequest request;
    img::ITexture* tex = queueTexture(file, request, m_memory);
    int bufferIndex = acquireStaging();
    request.buffIndex = bufferIndex;
    co_await m_ios.submit(request);

    tex->initFromBuffer(m_ios.getMemPtr(bufferIndex));

    TextureDesc desc = tex->desc();
    std::span levels = tex->levels();
    uint32_t head = tex->headOffset();
    desc.debugName = file->getFilename();
    texture = m_device->createTexture(desc);
    uint32_t texId = texturePool->allocate();
    texturePool->setTexture(texture, texId);

    // BufferPtr staging = m_device->createHostBuffer(tex->getDataSize());
    // auto* buff = checked_cast<Buffer*>(staging.get());
    // auto* data = static_cast<std::byte*>(buff->hostInfo.pMappedData);

    // BufferPtr staging = getStaging(kStagingSize);

    request.fileLength = tex->getDataSize();
    request.fileOffset = head;
    // request.buffAddress = data;
    request.buffIndex = bufferIndex;

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    for (const size_t mip : std::views::iota(0u, levels.size()))
    {
        const img::ITexture::LevelIndexEntry& level = levels[mip];
        log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

        Subresource sub;
        sub.index = mip;
        sub.offset = level.byteOffset - head;
        // sub.offset = level.byteOffset;
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

coro::task<> Storage::makeMultiTextureTask(coro::latch& latch, TexturePoolPtr texturePool,
                                               const std::vector<ReadOnlyFilePtr> files)
{
    /*
    std::vector<img::ITexture*> images;
    images.reserve(files.size());
    std::vector<sys::IoService::FileLoadRequest> requests(files.size());
    for (int i = 0; i < files.size(); ++i)
        images.emplace_back(queueTexture(files[i], requests[i], m_memory));

    co_await m_ios.submit(requests);

    std::vector<BufferPtr> stagings;

    uint32_t capacity = 0;
    uint32_t byteSizes = 0;
    uint32_t buffId;

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

    uint32_t offset;
    for (size_t i = 0; i < files.size(); ++i)
    {
        auto& file = files[i];
        img::ITexture* tex = images[i];
        tex->init();
        std::span levels = tex->levels();
        TextureDesc desc = tex->desc();
        desc.debugName = file->getFilename();
        FormatBlockInfo blockInfo = formatToBlockInfo(tex->getFormat());

        log::info("Load texture: {}", desc.debugName);
        TexturePtr texture = m_device->createTexture(desc);
        uint32_t texIndex = texturePool->allocate();
        texturePool->setTexture(texture, texIndex);

        byteSizes += align(tex->getDataSize(), blockInfo.blockSizeByte);

        std::byte* data = nullptr;
        if (byteSizes > capacity)
        {
            offset = 0;
            BufferPtr staging = getStaging(kStagingSize);
            capacity += kStagingSize;

            auto* buff = checked_cast<Buffer*>(staging.get());
            data = static_cast<std::byte*>(buff->hostInfo.pMappedData);
            stagings.emplace_back(staging);
        }

        uint32_t head = tex->headOffset();
        for (const size_t mip : std::views::iota(0u, levels.size()))
        {
            const img::ITexture::LevelIndexEntry& level = levels[mip];
            // log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

            Subresource sub;
            sub.index = mip;
            sub.offset = level.byteOffset - head + offset;
            sub.width = desc.width >> mip;
            sub.height = desc.height >> mip;
            cmd->copyBufferToTexture(stagings.back(), texture, sub, nullptr);
        }

        sys::IoService::FileLoadRequest req = requests[i];
        req.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
        req.fileLength = tex->getDataSize();
        req.fileOffset = head;
        req.buffOffset = offset;
        req.buffAddress = data;

        offset += align(tex->getDataSize(), blockInfo.blockSizeByte);
    }

    co_await m_ios.submit(requests);

    m_device->submitOneShot(cmd);

    latch.count_down();
    for (BufferPtr& buffer : stagings)
        putStaging(buffer);

    co_return;*/

    // ***********************************************************
    // V2
    std::vector<uint32_t> stagings;

    constexpr uint32_t capacity = kStagingSize;
    uint32_t offset = capacity;
    int bufferId;

    std::vector<img::ITexture*> images;
    images.reserve(files.size());
    std::vector<sys::IoService::FileLoadRequest> requests(files.size());
    for (int i = 0; i < files.size(); ++i)
    {
        images.emplace_back(queueTexture(files[i], requests[i], m_memory));
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
        img::ITexture* tex = images[i];
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
    /*std::vector<uint32_t> stagings;

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

    co_await m_ios.submit(requests);*/

    m_device->submitOneShot(cmd);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}

coro::task<> Storage::makeBufferTask(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer,
                                         uint32_t fileLength, uint32_t fileOffset)
{
    const int bufferId = acquireStaging();
    BufferPtr staging = getStaging(bufferId);

    auto* buff = checked_cast<Buffer*>(staging.get());
    auto* data = static_cast<std::byte*>(buff->hostInfo.pMappedData);

    sys::IoService::FileLoadRequest request;
    request.file = &checked_cast<ReadOnlyFile*>(file.get())->handle;
    request.fileLength = fileLength;
    request.fileOffset = fileOffset;
    request.buffIndex = bufferId;
    //request.buffAddress = data;

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    cmd->copyBuffer(staging, buffer, fileLength, 0);

    co_await m_ios.submit(request);

    m_device->submitOneShot(cmd);

    latch.count_down();
    releaseStaging(bufferId);
}

void Storage::requestLoadTexture(coro::latch& latch, TexturePoolPtr& texturePool,
                                 const std::span<ReadOnlyFilePtr>& files)
{
    if (files.size() == 1)
        m_scheduler->start(makeSingleTextureTask(latch, texturePool, files.front()));
    else
    {
        std::vector fileList(files.begin(), files.end());
        m_scheduler->start(makeMultiTextureTask(latch, texturePool, std::move(fileList)));
    }
}

void Storage::requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer, uint32_t fileLength,
                                uint32_t fileOffset)
{
    m_scheduler->start(makeBufferTask(latch, file, buffer, fileLength, fileOffset));
}
} // namespace ler::rhi::vulkan