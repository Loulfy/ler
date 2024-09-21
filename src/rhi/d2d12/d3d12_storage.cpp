#include "rhi/d3d12.hpp"

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
        auto* buff = checked_cast<Buffer*>(staging.get());
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
    int bufferIndex = acquireStaging();
    request.Destination.Memory.Buffer = m_buffers[bufferIndex];
    request.Destination.Memory.Size = file->sizeBytes();
    request.Source.File.Size = file->sizeBytes();
    m_queue->EnqueueRequest(&request);

    submitWait();

    img::ITexture* tex = factoryTexture(file, m_buffers[bufferIndex]);

    TextureDesc desc = tex->desc();
    std::span levels = tex->levels();
    desc.debugName = file->getFilename();
    texture = m_device->createTexture(desc);
    uint32_t texId = table->allocate();
    table->setResource(texture, texId);

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);
    for (const size_t mip : std::views::iota(0u, levels.size()))
    {
        const img::ITexture::LevelIndexEntry& level = levels[mip];
        log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

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
        const uint32_t byteSizes = align(files[i]->sizeBytes(), 16u);

        if (offset + byteSizes > capacity)
        {
            offset = 0;
            bufferId = acquireStaging();
            stagings.emplace_back(bufferId);
        }

        dependencies[i] = { bufferId, offset };
        requests[i].Destination.Memory.Buffer = m_buffers[bufferId] + offset;
        requests[i].Destination.Memory.Size = files[i]->sizeBytes();
        requests[i].Source.File.Size = files[i]->sizeBytes();
        m_queue->EnqueueRequest(&requests[i]);

        offset += byteSizes;
    }

    submitWait();

    CommandPtr cmd = m_device->createCommand(QueueType::Transfer);

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

        uint32_t texIndex = table->allocate();
        log::info("Load texture {:03}: {}", texIndex, desc.debugName);
        TexturePtr texture = m_device->createTexture(desc);
        table->setResource(texture, texIndex);

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
            cmd->copyBufferToTexture(getStaging(0), texture, sub, nullptr);
        }
    }

    m_device->submitOneShot(cmd);

    latch.count_down();
    for (const uint32_t buffId : stagings)
        releaseStaging(buffId);

    co_return;
}

coro::task<> Storage::makeBufferTask(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer,
                                     uint32_t fileLength, uint32_t fileOffset)
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