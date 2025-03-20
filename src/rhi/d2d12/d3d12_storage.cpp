#include "rhi/d3d12.hpp"

#define DDSKTX_IMPLEMENT
#include "dds-ktx.h"

namespace ler::rhi::d3d12
{
Storage::Storage(Device* device, std::shared_ptr<coro::thread_pool>& tp)
    : CommonStorage(device, tp), m_context(device->getContext())
{
    // Create a DirectStorage queue which will be used to load data into a
    // buffer on the GPU.
    DSTORAGE_QUEUE_DESC queueDesc = {};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_HIGH;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    queueDesc.Device = m_context.device;

    m_context.storage->SetStagingBufferSize(256 * 1024 * 1024);
    m_context.storage->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_queue));

    for (const BufferPtr& staging : m_stagings)
    {
        const auto* buff = checked_cast<Buffer*>(staging.get());
        void* pMappedData;
        buff->handle->Map(0, nullptr, &pMappedData);
        auto* data = static_cast<std::byte*>(pMappedData);
        m_buffers.emplace_back(data);
    }
}

ReadOnlyFilePtr Storage::openFile(const fs::path& path)
{
    auto file = std::make_shared<ReadOnlyFile>();
    m_context.storage->OpenFile(path.c_str(), IID_PPV_ARGS(&file->handle));
    file->path = path;
    file->filename = path.filename().string();
    return file;
}

void Storage::submitWait()
{
    ComPtr<ID3D12Fence> fence;
    m_context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);

    m_queue->EnqueueSignal(fence.Get(), 1);
    m_queue->Submit();
    WaitForSingleObjectEx(event, INFINITE, FALSE);
    CloseHandle(event);

    DSTORAGE_ERROR_RECORD errorRecord = {};
    m_queue->RetrieveErrorRecord(&errorRecord);
    if (FAILED(errorRecord.FirstFailure.HResult))
    {
        // errorRecord.FailureCount - The number of failed requests in the queue since the last
        //                            RetrieveErrorRecord call.
        // errorRecord.FirstFailure - Detailed record about the first failed command in the enqueue order.
        log::error("The DirectStorage request failed! HRESULT={}", errorRecord.FirstFailure.HResult);
        log::error(getErrorMsg(errorRecord.FirstFailure.HResult));
    }
}

static void queueTexture(const ReadOnlyFilePtr& file, DSTORAGE_REQUEST& req)
{
    const fs::path filename(file->getFilename());
    std::string ext = filename.extension().string();
    std::ranges::transform(ext, ext.begin(), ::tolower);

    req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    req.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    req.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
    req.Source.File.Source = checked_cast<ReadOnlyFile*>(file.get())->handle.Get();
    req.Source.File.Offset = 0;

    if (ext == ".dds")
        req.Source.File.Size = img::DdsTexture::kBytesToRead;
    else if (ext == ".ktx" || ext == ".ktx2")
        req.Source.File.Size = img::KtxTexture::kBytesToRead;
    req.Destination.Memory.Size = req.Source.File.Size;
}

coro::task<> Storage::makeSingleTextureTask(coro::latch& latch, BindlessTablePtr table, ReadOnlyFilePtr file)
{
    TexturePtr texture;
    DSTORAGE_REQUEST request = {};
    queueTexture(file, request);
    int bufferIndex = co_await acquireStaging();
    request.Destination.Memory.Buffer = m_buffers[bufferIndex];
    request.Destination.Memory.Size = file->sizeInBytes();
    request.Source.File.Size = file->sizeInBytes();
    m_queue->EnqueueRequest(&request);

    submitWait();

    ddsktx_texture_info tc = { 0 };
    bool succeeded = ddsktx_parse(&tc, m_buffers[bufferIndex], file->sizeInBytes(), nullptr);

    img::ITexture* tex = factoryTexture(file, m_buffers[bufferIndex]);

    TextureDesc desc = tex->desc();
    std::span levels = tex->levels();
    desc.debugName = file->getFilename();
    texture = m_device->createTexture(desc);

    // uint32_t texId = table->allocate();
    // table->setResource(texture, texId);

    {
        std::lock_guard lock = table->lock();
        ResourceViewPtr view = table->createResourceView(texture);
    }

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    for (const size_t mip : std::views::iota(0u, levels.size()))
    {
        const img::ITexture::LevelIndexEntry& level = levels[mip];
        log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

        /*request.Destination.Memory.Buffer = ptr + size;
        request.Destination.Memory.Size = level.byteLength;
        request.Source.File.Size = level.byteLength;
        request.Source.File.Offset = level.byteOffset;
        m_queue->EnqueueRequest(&request);*/

        ddsktx_sub_data sub_data;
        ddsktx_get_sub(&tc, &sub_data, m_buffers[bufferIndex], file->sizeInBytes(), 0, 0, mip);
        log::info("{} - {}", tex->getRowPitch(mip), sub_data.row_pitch_bytes);

        Subresource sub;
        sub.index = mip;
        sub.offset = level.byteOffset;
        sub.width = desc.width >> mip;
        sub.height = desc.height >> mip;
        sub.rowPitch = tex->getRowPitch(mip);
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
        uint32_t offset = 0;
    };

    // std::vector<img::ITexture*> images;
    // images.reserve(files.size());
    std::vector<DepRequest> dependencies(files.size());
    std::vector<DSTORAGE_REQUEST> requests(files.size());
    for (int i = 0; i < files.size(); ++i)
    {
        queueTexture(files[i], requests[i]);
        const uint32_t byteSizes = files[i]->sizeInBytes(); //, 16u);

        if (offset + byteSizes > capacity)
        {
            offset = 0;
            bufferId = co_await acquireStaging();
            stagings.emplace_back(bufferId);
        }

        dependencies[i] = { bufferId, offset };
        requests[i].Destination.Memory.Buffer = m_buffers[bufferId] + offset;
        requests[i].Destination.Memory.Size = files[i]->sizeInBytes();
        requests[i].Source.File.Size = files[i]->sizeInBytes();
        m_queue->EnqueueRequest(&requests[i]);

        offset += byteSizes;
    }

    submitWait();

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    std::vector<TextureStreaming> result;
    result.reserve(files.size());

    {
        std::lock_guard lock = table->lock();
        for (size_t i = 0; i < files.size(); ++i)
        {
            const auto& file = files[i];
            const DepRequest& dep = dependencies[i];
            const DSTORAGE_REQUEST& req = requests[i];
            std::byte* data = m_buffers[dep.bufferId];
            // img::ITexture* tex = images[i];
            // tex->initFromBuffer(data + req.buffOffset);
            img::ITexture* tex = factoryTexture(file, data + dep.offset);
            std::span levels = tex->levels();
            TextureDesc desc = tex->desc();
            desc.debugName = file->getFilename();

            // uint32_t texIndex = table->allocate();
            TexturePtr texture = m_device->createTexture(desc);
            result.emplace_back(table->createResourceView(texture), desc.debugName);
            log::info("Load texture {:03}: {}", result.back().view->getBindlessIndex(), desc.debugName);
            // table->setResource(texture, texIndex);

            for (const size_t mip : std::views::iota(0u, levels.size()))
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
    }

    m_device->submitOneShot(cmd);
    m_dispatcher.enqueue(result);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}

coro::task<> Storage::makeMultiTextureTask(coro::latch& latch, BindlessTablePtr table,
                                           std::vector<TextureStreamingMetadata> textures)
{
    std::vector<TextureStreaming> result;
    result.reserve(textures.size());

    {
        std::lock_guard lock = table->lock();
        for (const TextureStreamingMetadata& metadata : textures)
        {
            TexturePtr texture = m_device->createTexture(metadata.desc);
            result.emplace_back(table->createResourceView(texture), metadata.desc.debugName);
            log::info("Load texture {:03}: {}", result.back().view->getBindlessIndex(), metadata.desc.debugName);

            DSTORAGE_REQUEST request = {};
            request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
            request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
            request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
            request.Source.File.Source = checked_cast<ReadOnlyFile*>(metadata.file.get())->handle.Get();
            request.Source.File.Offset = metadata.byteOffset;
            request.Source.File.Size = metadata.byteLength;

            request.Destination.MultipleSubresources.FirstSubresource = 0;
            request.Destination.MultipleSubresources.Resource = checked_cast<Texture*>(texture.get())->handle;
            m_queue->EnqueueRequest(&request);
        }
    }

    submitWait();

    auto resCountDown = static_cast<int64_t>(result.size());
    m_dispatcher.enqueue(result);

    latch.count_down(resCountDown);
    co_return;
}

coro::task<> Storage::makeBufferTask(coro::latch& latch, ReadOnlyFilePtr file, BufferPtr buffer, uint64_t fileLength,
                                     uint64_t fileOffset)
{
    auto* f = checked_cast<ReadOnlyFile*>(file.get());

    DSTORAGE_REQUEST request = {};
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Source.File.Source = f->handle.Get();
    request.Source.File.Offset = fileOffset;
    request.Source.File.Size = fileLength;
    request.Destination.Buffer.Resource = checked_cast<Buffer*>(buffer.get())->handle;
    request.Destination.Buffer.Offset = 0;
    request.Destination.Buffer.Size = fileLength;
    m_queue->EnqueueRequest(&request);

    submitWait();
    latch.count_down();

    co_return;
}
} // namespace ler::rhi::d3d12