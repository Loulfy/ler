//
// Created by Loulfy on 01/12/2023.
//

#include "log/log.hpp"
#include "rhi/d3d12.hpp"
#include "sys/utils.hpp"

namespace ler::rhi::d3d12
{
void messageCallback(D3D12_MESSAGE_CATEGORY Category, D3D12_MESSAGE_SEVERITY Severity, D3D12_MESSAGE_ID ID,
                     LPCSTR pDescription, void* pContext)
{
    log::error(pDescription);
}

std::string getErrorMsg(HRESULT hr)
{
    LPSTR messageBuffer = nullptr;
    size_t size =
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&messageBuffer, 0, nullptr);
    // Copy the error message into a std::string.
    std::string str(messageBuffer, size);
    LocalFree(messageBuffer);
    return str;
}

DevicePtr CreateDevice(const DeviceConfig& config)
{
    return std::make_shared<Device>(config);
}

static constexpr std::string d3dFeatureToString(D3D_FEATURE_LEVEL level)
{
    switch (level)
    {
    default:
        return "Outdated";
    case D3D_FEATURE_LEVEL_12_0:
        return "12.0";
    case D3D_FEATURE_LEVEL_12_1:
        return "12.1";
    case D3D_FEATURE_LEVEL_12_2:
        return "12.2";
    }
}

Device::~Device()
{
    for(auto& sb : m_context.scratchBufferPool)
        sb.reset();
    m_storage.reset();
    m_allocator->Release();
}

Device::Device(const DeviceConfig& config)
{
    UINT dxgiFactoryFlags = 0;
    ComPtr<ID3D12Debug> debugController;
    if (config.debug && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();

        // Enable additional debug layers.
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory));

    const D3D_FEATURE_LEVEL sdk = D3D_FEATURE_LEVEL_12_2;
    log::info("DirectX SDK {}", d3dFeatureToString(sdk));

    const DXGI_GPU_PREFERENCE pref = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
    for (UINT adapterIndex = 0;
         SUCCEEDED(m_factory->EnumAdapterByGpuPreference(adapterIndex, pref, IID_PPV_ARGS(&m_adapter))); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        m_adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (SUCCEEDED(D3D12CreateDevice(m_adapter.Get(), sdk, IID_PPV_ARGS(&m_device))))
        {
            std::string deviceName = sys::toUtf8(desc.Description);
            log::info("GPU: {}", deviceName);

            break;
        }
    }

    if (m_device == nullptr)
        log::exit("No discrete GPU available");

    HRESULT hr;
    if (config.debug)
    {
        DWORD cookie;
        hr = m_device->QueryInterface<ID3D12InfoQueue1>(&m_infoQueue);
        assert(SUCCEEDED(hr));
        m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        hr = m_infoQueue->RegisterMessageCallback(&messageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
        assert(SUCCEEDED(hr));
    }

    hr = DStorageGetFactory(IID_PPV_ARGS(&m_dStorage));
    assert(SUCCEEDED(hr));

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
    bool supportRayTracing = options.RaytracingTier == D3D12_RAYTRACING_TIER_1_1;
    log::info("Support Ray Tracing: {}", supportRayTracing);

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 features1 = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &features1, sizeof(features1));
    log::info("SubgroupSize: {}", features1.WaveLaneCountMax);

    D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
    allocatorDesc.pDevice = m_device.Get();
    allocatorDesc.pAdapter = m_adapter.Get();
    D3D12MA::CreateAllocator(&allocatorDesc, &m_allocator);

    m_context.device = m_device.Get();
    m_context.adapter = m_adapter.Get();
    m_context.storage = m_dStorage.Get();

    for (size_t i = 0; i < m_context.descriptorPool.size(); ++i)
    {
        m_context.descriptorPool[i] = std::make_unique<DescriptorHeapAllocator>();
        m_context.descriptorPool[i]->create(m_device.Get(), c_DescriptorPoolSize[i].type,
                                            c_DescriptorPoolSize[i].count);
    }

    m_context.descriptorPoolCpuOnly = std::make_unique<DescriptorHeapAllocator>();
    m_context.descriptorPoolCpuOnly->create(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, false);

    m_queues[static_cast<int>(QueueType::Graphics)] = std::make_unique<Queue>(m_context, QueueType::Graphics);
    m_queues[static_cast<int>(QueueType::Transfer)] = std::make_unique<Queue>(m_context, QueueType::Transfer);

    m_context.queue = m_queues[static_cast<int>(QueueType::Graphics)]->m_commandQueue.Get();

    for(std::unique_ptr<ScratchBuffer>& scratchBuffer : m_context.scratchBufferPool)
        scratchBuffer = std::make_unique<ScratchBuffer>(this);

    m_threadPool = std::make_shared<coro::thread_pool>(coro::thread_pool::options{ .thread_count = 8 });

    m_storage = std::make_shared<Storage>(this, m_threadPool);
    m_library = std::make_shared<PSOLibrary>(this);
}

void Buffer::uploadFromMemory(const void* src, uint64_t byteSize) const
{
    void* pMappedData;
    if (staging() && sizeInBytes() >= byteSize && SUCCEEDED(handle->Map(0, nullptr, &pMappedData)))
    {
        memcpy(pMappedData, src, byteSize);
        handle->Unmap(0, nullptr);
    }
    else
        log::error("Failed to upload to buffer");
}

void Buffer::getUint(uint32_t* ptr) const
{
    void* pMappedData;
    if (staging() && SUCCEEDED(handle->Map(0, nullptr, &pMappedData)))
    {
        auto data = (uint32_t*)pMappedData;
        *ptr = data[1];
        handle->Unmap(0, nullptr);
    }
    else
        log::error("Failed to upload to buffer");
}

BufferPtr Device::createBuffer(uint64_t byteSize, bool staging)
{
    auto buffer = std::make_shared<Buffer>();

    buffer->desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer->desc.Alignment = 0;
    buffer->desc.Width = byteSize;
    buffer->desc.Height = 1;
    buffer->desc.DepthOrArraySize = 1;
    buffer->desc.MipLevels = 1;
    buffer->desc.Format = DXGI_FORMAT_UNKNOWN;
    buffer->desc.SampleDesc.Count = 1;
    buffer->desc.SampleDesc.Quality = 0;
    buffer->desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer->desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    buffer->allocDesc.HeapType = staging ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = m_allocator->CreateResource(&buffer->allocDesc, &buffer->desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr, &buffer->allocation, IID_NULL, nullptr);

    if (FAILED(hr))
        log::error("Failed to allocate buffer");
    else
        buffer->handle = buffer->allocation->GetResource();

    return buffer;
}

BufferPtr Device::createBuffer(const BufferDesc& desc)
{
    auto buffer = std::make_shared<Buffer>();

    buffer->format = convertFormatRtv(desc.format);
    buffer->isCBV = desc.isConstantBuffer;
    buffer->desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer->desc.Alignment = desc.alignment;
    buffer->desc.Width = desc.sizeInBytes;
    buffer->desc.Height = 1;
    buffer->desc.DepthOrArraySize = 1;
    buffer->desc.MipLevels = 1;
    buffer->desc.Format = DXGI_FORMAT_UNKNOWN;
    buffer->desc.SampleDesc.Count = 1;
    buffer->desc.SampleDesc.Quality = 0;
    buffer->desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer->desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    buffer->stride = desc.stride;
    buffer->state = Common;
    if (desc.isUAV)
    {
        buffer->desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        buffer->clearCpuHandle = m_context.descriptorPoolCpuOnly->allocate(1);
        buffer->clearGpuHandle = m_context.descriptorPool[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->allocate(1);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        if (desc.stride > 0)
        {
            uavDesc.Buffer.StructureByteStride = desc.stride;
            uavDesc.Buffer.NumElements = desc.sizeInBytes / desc.stride;
        }
        else
        {
            uavDesc.Format = DXGI_FORMAT_R32_UINT;
            uavDesc.Buffer.NumElements = desc.sizeInBytes / sizeof(uint32_t);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = buffer->clearGpuHandle.getCpuHandle();
        D3D12_CPU_DESCRIPTOR_HANDLE cpuOnlyHandle = buffer->clearCpuHandle.getCpuHandle();
        m_context.device->CreateUnorderedAccessView(buffer->handle, nullptr, &uavDesc, cpuOnlyHandle);
        m_context.device->CopyDescriptorsSimple(1, cpuHandle, cpuOnlyHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (desc.isConstantBuffer)
        buffer->desc.Width = align(desc.sizeInBytes, 256ull); // CB size is required to be 256-byte aligned.

    buffer->allocDesc.HeapType = desc.isStaging ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
    buffer->allocDesc.HeapType = desc.isReadBack ? D3D12_HEAP_TYPE_READBACK : buffer->allocDesc.HeapType;

    HRESULT hr = m_allocator->CreateResource(&buffer->allocDesc, &buffer->desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                             &buffer->allocation, IID_NULL, nullptr);

    if (FAILED(hr))
        log::error("Failed to allocate buffer");
    else
        buffer->handle = buffer->allocation->GetResource();

    std::wstring name = sys::toUtf16(desc.debugName);
    buffer->handle->SetName(name.c_str());
    return buffer;
}

BufferPtr Device::createHostBuffer(uint64_t byteSize)
{
    return createBuffer(byteSize, true);
}

TexturePtr Device::createTexture(const TextureDesc& desc)
{
    auto texture = std::make_shared<Texture>();
    populateTexture(texture, desc);
    return texture;
}

void Device::waitIdle()
{
    ComPtr<ID3D12Fence> fence;
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Schedule a Signal command in the queue.
    m_context.queue->Signal(fence.Get(), 1);

    // Wait until the fence has been processed.
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObjectEx(event, INFINITE, FALSE);
    CloseHandle(event);
}
} // namespace ler::rhi::d3d12