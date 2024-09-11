#include "rhi/d3d12.hpp"

namespace ler::rhi::d3d12
{
Storage::Storage(const D3D12Context& context) : m_context(context)
{
    // Create a DirectStorage queue which will be used to load data into a
    // buffer on the GPU.
    DSTORAGE_QUEUE_DESC queueDesc = {};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_HIGH;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    queueDesc.Device = context.device;

    context.storage->SetStagingBufferSize(256 * 1024 * 1024);
    context.storage->CreateQueue(&queueDesc, IID_PPV_ARGS(&queue));
}

Storage::~Storage()
{
}

ReadOnlyFilePtr Storage::openFile(const fs::path& path)
{
    auto file = std::make_shared<ReadOnlyFile>();
    m_context.storage->OpenFile(path.c_str(), IID_PPV_ARGS(&file->handle));
    file->filename = path.filename().string();
    return file;
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

void Storage::update()
{
    m_requests.remove_if([](StorageRequest& req) {
        if (req.status && req.status->IsComplete(0))
        {
            req.handle.resume();
            return true;
        }
        return false;
    });
}

img::KtxTexture* Storage::enqueueLoadKtx(const ReadOnlyFilePtr& file)
{
    auto* f = checked_cast<ReadOnlyFile*>(file.get());

    BY_HANDLE_FILE_INFORMATION info;
    f->handle.Get()->GetFileInformation(&info);
    assert(info.nFileSizeLow > img::KtxTexture::kBytesToRead);

    KtxAllocator alloc(&m_memory);
    auto* image = alloc.new_object<img::KtxTexture>();

    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
    request.Source.File.Source = f->handle.Get();
    request.Source.File.Offset = 0;
    request.Source.File.Size = img::KtxTexture::kBytesToRead;
    request.Destination.Memory.Buffer = image->identifier;
    request.Destination.Memory.Size = img::KtxTexture::kBytesToRead;
    queue->EnqueueRequest(&request);

    return image;
}

img::DdsTexture* Storage::enqueueLoadDds(const ReadOnlyFilePtr& file)
{
    auto* f = checked_cast<ReadOnlyFile*>(file.get());

    BY_HANDLE_FILE_INFORMATION info;
    f->handle.Get()->GetFileInformation(&info);
    assert(info.nFileSizeLow > img::DdsTexture::kBytesToRead);

    KtxAllocator alloc(&m_memory);
    auto* image = alloc.new_object<img::DdsTexture>();

    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
    request.Options.CompressionFormat = DSTORAGE_COMPRESSION_FORMAT_NONE;
    request.Source.File.Source = f->handle.Get();
    request.Source.File.Offset = 0;
    request.Source.File.Size = img::DdsTexture::kBytesToRead;
    request.Destination.Memory.Buffer = &image->ddsMagic;
    request.Destination.Memory.Size = img::DdsTexture::kBytesToRead;
    queue->EnqueueRequest(&request);

    return image;
}

void Storage::enqueueLoadTex(const ReadOnlyFilePtr& file, BufferPtr& staging, img::ITexture* tex, uint32_t offset) const
{
    auto* f = checked_cast<ReadOnlyFile*>(file.get());

    DSTORAGE_REQUEST request = {};
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Source.File.Source = f->handle.Get();
    request.Source.File.Offset = tex->headOffset();
    request.Source.File.Size = tex->getDataSize();
    request.Destination.Buffer.Resource = checked_cast<Buffer*>(staging.get())->handle;
    request.Destination.Buffer.Offset = offset;
    request.Destination.Buffer.Size = tex->getDataSize();

    queue->EnqueueRequest(&request);
}

Storage::IoReadAwaiter Storage::submitAwaitable()
{
    StorageRequest& batch = m_requests.emplace_back();
    HRESULT hr = m_context.storage->CreateStatusArray(1, "", IID_PPV_ARGS(&batch.status));
    assert(SUCCEEDED(hr));
    queue->EnqueueStatus(batch.status.Get(), 0);
    queue->Submit();

    return IoReadAwaiter(&m_requests.back());
}

void Storage::submitSync()
{
    ComPtr<ID3D12Fence> fence;
    m_context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);

    queue->EnqueueSignal(fence.Get(), 1);
    queue->Submit();
    WaitForSingleObjectEx(event, INFINITE, FALSE);
    CloseHandle(event);
}

/*async::task<TexturePtr> Storage::asyncLoadTexture(const ReadOnlyFilePtr& file)
{
    TexturePtr texture;

    img::ITexture* image = enqueueLoadKtx(file);

    //co_await submitAwaitable();
    submitSync();

    image->init();

    img::ITexture* tex = image;
    std::span levels = tex->levels();
    TextureDesc desc = tex->desc();
    desc.debugName = file->getFilename();
    texture = m_device->createTexture(desc);

    auto command = m_device->createCommand(QueueType::Transfer);
    auto staging = m_device->createBuffer(tex->getDataSize(), true);

    uint32_t head = tex->headOffset();
    for (const size_t mip : std::views::iota(0u, levels.size()))
    {
        const img::ITexture::LevelIndexEntry& level = levels[mip];
        // log::info("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

        Subresource sub;
        sub.index = mip;
        sub.offset = level.byteOffset - head;
        sub.width = desc.width >> mip;
        sub.height = desc.height >> mip;
        sub.rowPitch = tex->getRowPitch(mip);
        command->copyBufferToTexture(staging, texture, sub, nullptr);
    }

    enqueueLoadTex(file, staging, image, 0);

    // co_await submitAwaitable();
    submitSync();

    // m_device->submitCommand(command);
    // co_await SubmitAsync(command);
    m_device->submitOneShot(command);

    co_return texture;
}

async::task<Result> Storage::asyncLoadTexturePool(const std::vector<ReadOnlyFilePtr>& files,
                                                  TexturePoolPtr& texturePool)
{
    auto pool = texturePool.get();
    auto images = std::make_unique<std::vector<img::ITexture*>>();
    images->reserve(files.size());
    for (auto& f : files)
    {
        if (f->getFilename().ends_with(".dds") || f->getFilename().ends_with(".DDS"))
            images->emplace_back(enqueueLoadDds(f));
        else if (f->getFilename().ends_with(".ktx"))
            images->emplace_back(enqueueLoadKtx(f));
    }

    co_await submitAwaitable();

    DSTORAGE_ERROR_RECORD errorRecord = {};
    queue->RetrieveErrorRecord(&errorRecord);
    if (FAILED(errorRecord.FirstFailure.HResult))
    {
        //
        // errorRecord.FailureCount - The number of failed requests in the queue since the last
        //                            RetrieveErrorRecord call.
        // errorRecord.FirstFailure - Detailed record about the first failed command in the enqueue order.
        //
        log::error("The DirectStorage request failed! HRESULT={}", errorRecord.FirstFailure.HResult);
        log::error(getErrorMsg(errorRecord.FirstFailure.HResult));
    }

    uint32_t byteSize = 0;
    for (img::ITexture* img : *images)
    {
        img->init();
        byteSize += img->getDataSize();
    }

    CommandPtr command = m_device->createCommand(QueueType::Transfer);
    BufferPtr staging = m_device->createBuffer(byteSize, true);

    uint32_t offset = 0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        auto& file = files[i];
        img::ITexture* tex = images->at(i);
        std::span levels = tex->levels();
        TextureDesc desc = tex->desc();
        desc.debugName = file->getFilename();

        log::info("Load texture: {}", desc.debugName);
        auto texture = m_device->createTexture(desc);
        uint32_t texIndex = pool->allocate();

        uint32_t head = tex->headOffset();
        for (const size_t mip : std::views::iota(0u, levels.size()))
        {
            const img::ITexture::LevelIndexEntry& level = levels[mip];
            // log::debug("byteOffset = {}, byteLength = {}", level.byteOffset, level.byteLength);

            Subresource sub;
            sub.index = mip;
            sub.offset = level.byteOffset - head + offset;
            sub.width = desc.width >> mip;
            sub.height = desc.height >> mip;
            sub.rowPitch = tex->getRowPitch(mip);
            command->copyBufferToTexture(staging, texture, sub, nullptr);
        }

        enqueueLoadTex(file, staging, tex, offset);
        pool->setTexture(texture, texIndex);
        offset += tex->getDataSize();
    }

    co_await submitAwaitable();

    // m_device->submitCommand(command);
    // co_await SubmitAsync(command);
    m_device->submitOneShot(command);

    co_return { true, "" };
}

async::task<Result> Storage::asyncLoadBuffer(const ReadOnlyFilePtr& file, BufferPtr& buffer, uint32_t fileLength,
                                             uint32_t fileOffset)
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
    queue->EnqueueRequest(&request);

    co_await submitAwaitable();

    co_return { true, "" };
}*/

void Storage::requestLoadTexture(coro::latch& latch, TexturePoolPtr& texturePool, const std::span<ReadOnlyFilePtr>& files)
{

}

void Storage::requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer, uint32_t fileLength, uint32_t fileOffset)
{

}
} // namespace ler::rhi::d3d12