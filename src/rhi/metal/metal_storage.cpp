//
// Created by Loulfy on 03/01/2025.
//

#include "rhi/metal.hpp"

#include <sys/stat.h>

namespace ler::rhi::metal
{
std::string ReadOnlyFile::getFilename()
{
    return path.filename();
}

uint64_t ReadOnlyFile::sizeInBytes()
{
    struct stat st = {};
    stat(path.c_str(), &st);
    return st.st_size;
}

Storage::Storage(Device* device, std::shared_ptr<coro::thread_pool>& tp)
    : CommonStorage(device, tp), m_context(device->getContext())
{
    MTL::IOCommandQueueDescriptor* desc = MTL::IOCommandQueueDescriptor::alloc()->init();
    desc->setType(MTL::IOCommandQueueTypeConcurrent);
    desc->setPriority(MTL::IOPriorityNormal);

    NS::Error* error;
    m_queue = NS::TransferPtr(m_context.device->newIOCommandQueue(desc, &error));
    desc->release();

    for (const BufferPtr& staging : m_stagings)
    {
        auto* buff = checked_cast<Buffer*>(staging.get());
        auto* data = static_cast<std::byte*>(buff->handle->contents());
        m_buffers.emplace_back(data);
    }
}

ReadOnlyFilePtr Storage::openFile(const fs::path& path)
{
    auto file = std::make_shared<ReadOnlyFile>();

    NS::Error* error;
    const auto url = NS::URL::fileURLWithPath(NS::String::string(path.c_str(), NS::StringEncoding::UTF8StringEncoding));
    file->handle = m_context.device->newIOFileHandle(url, &error);
    file->path = path;
    url->release();
    return file;
}

coro::task<> Storage::makeSingleTextureTask(coro::latch& latch, BindlessTablePtr table, ReadOnlyFilePtr file)
{
    TexturePtr texture;
    MTL::IOCommandBuffer* request = m_queue->commandBuffer();
    //queueTexture(file, request);
    int bufferIndex = acquireStaging();

    MTL::IOFileHandle* srcFile = checked_cast<ReadOnlyFile*>(file.get())->handle;
    request->loadBytes(m_buffers[bufferIndex], file->sizeInBytes(), srcFile, 0);

    request->commit();
    request->waitUntilCompleted();

    img::ITexture* tex = factoryTexture(file, m_buffers[bufferIndex]);

    TextureDesc desc = tex->desc();
    std::span<const img::ITexture::LevelIndexEntry> levels = tex->levels();
    uint64_t head = tex->headOffset();
    desc.debugName = file->getFilename();
    texture = m_device->createTexture(desc);
    uint32_t texId = table->allocate();
    table->setResource(texture, texId);

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    for (const uint32_t mip : std::views::iota(0u, levels.size()))
    {
        const img::ITexture::LevelIndexEntry& level = levels[mip];
        log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

        Subresource sub;
        sub.index = mip;
        sub.offset = level.byteOffset;
        sub.width = desc.width >> mip;
        sub.height = desc.height >> mip;
        sub.rowPitch = tex->getRowPitch(mip);
        sub.slicePitch = level.byteLength;
        cmd->copyBufferToTexture(getStaging(bufferIndex), texture, sub, nullptr);
    }

    m_device->submitOneShot(cmd);

    latch.count_down();
    releaseStaging(bufferIndex);

    co_return;
}

coro::task<> Storage::makeMultiTextureTask(coro::latch& latch, BindlessTablePtr table,
                                           std::vector<ReadOnlyFilePtr> files)
{
    std::vector<uint32_t> stagings;

    constexpr uint32_t capacity = kStagingSize;
    uint32_t offset = capacity;
    int bufferId;

    struct DepRequest
    {
        int bufferId = 0;
        uint64_t offset = 0;
    };

    // std::vector<img::ITexture*> images;
    // images.reserve(files.size());
    std::vector<DepRequest> dependencies(files.size());
    MTL::IOCommandBuffer* request = m_queue->commandBuffer();
    for (int i = 0; i < files.size(); ++i)
    {
        MTL::IOFileHandle* srcFile = checked_cast<ReadOnlyFile*>(files[i].get())->handle;
        const uint64_t byteSizes = align(files[i]->sizeInBytes(), 16ull);

        if (offset + byteSizes > capacity)
        {
            offset = 0;
            bufferId = acquireStaging();
            stagings.emplace_back(bufferId);
        }

        dependencies[i] = { bufferId, offset };
        request->loadBytes(m_buffers[bufferId] + offset, files[i]->sizeInBytes(), srcFile, 0);

        offset += byteSizes;
    }

    request->commit();
    request->waitUntilCompleted();

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

    for (size_t i = 0; i < files.size(); ++i)
    {
        const auto& file = files[i];
        const DepRequest& dep = dependencies[i];
        //const DSTORAGE_REQUEST& req = requests[i];
        std::byte* data = m_buffers[dep.bufferId];
        // img::ITexture* tex = images[i];
        // tex->initFromBuffer(data + req.buffOffset);
        img::ITexture* tex = factoryTexture(file, data + dep.offset);
        std::span levels = tex->levels();
        TextureDesc desc = tex->desc();
        desc.debugName = file->getFilename();

        uint32_t texIndex = table->allocate();
        log::info("Load texture {:03}: {}", texIndex, desc.debugName);
        TexturePtr texture = m_device->createTexture(desc);
        table->setResource(texture, texIndex);

        for (const uint32_t mip : std::views::iota(0u, levels.size()))
        {
            const img::ITexture::LevelIndexEntry& level = levels[mip];
            // log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

            Subresource sub;
            sub.index = mip;
            sub.offset = level.byteOffset + dep.offset;
            sub.width = desc.width >> mip;
            sub.height = desc.height >> mip;
            sub.rowPitch = tex->getRowPitch(mip);
            cmd->copyBufferToTexture(getStaging(dep.bufferId), texture, sub, nullptr);
        }
    }

    m_device->submitOneShot(cmd);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}

coro::task<> Storage::makeBufferTask(coro::latch& latch, ReadOnlyFilePtr file, BufferPtr buffer, uint64_t fileLength, uint64_t fileOffset)
{
    const MTL::Buffer* dstBuffer = checked_cast<Buffer*>(buffer.get())->handle;
    const MTL::IOFileHandle* srcFile = checked_cast<ReadOnlyFile*>(file.get())->handle;

    MTL::IOCommandBuffer* cmd = m_queue->commandBuffer();
    cmd->loadBuffer(dstBuffer, 0, fileLength, srcFile, fileOffset);
    cmd->commit();
    cmd->waitUntilCompleted();

    latch.count_down();

    co_return;
}
} // namespace ler::rhi::metal
