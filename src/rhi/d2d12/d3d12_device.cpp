//
// Created by loulfy on 01/12/2023.
//

#include "rhi/d3d12.hpp"
#include "log/log.hpp"
#include "sys/utils.hpp"

namespace ler::rhi::d3d12
{
    void messageCallback(D3D12_MESSAGE_CATEGORY Category, D3D12_MESSAGE_SEVERITY Severity, D3D12_MESSAGE_ID ID, LPCSTR pDescription, void* pContext)
    {
        log::error(pDescription);
    }

    std::string getErrorMsg(HRESULT hr)
    {
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, hr, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPSTR)&messageBuffer, 0, nullptr);
        //Copy the error message into a std::string.
        std::string str(messageBuffer, size);
        LocalFree(messageBuffer);
        return str;
    }

    DevicePtr CreateDevice(const DeviceConfig& config)
    {
        return std::make_shared<Device>(config);
    }

    std::string d3dFeatureToString(D3D_FEATURE_LEVEL level)
    {
        switch(level)
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
        //m_storage.reset();
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
        for (UINT adapterIndex = 0; SUCCEEDED(m_factory->EnumAdapterByGpuPreference(adapterIndex, pref, IID_PPV_ARGS(&m_adapter))); ++adapterIndex)
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

        if(m_device == nullptr)
            log::exit("No discrete GPU available");

        HRESULT hr;
        if(config.debug)
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
        m_device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
        bool supportRayTracing = options.RaytracingTier == D3D12_RAYTRACING_TIER_1_1;
        log::info("Support Ray Tracing: {}", supportRayTracing);

        D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
        allocatorDesc.pDevice = m_device.Get();
        allocatorDesc.pAdapter = m_adapter.Get();
        D3D12MA::CreateAllocator(&allocatorDesc, &m_allocator);

        m_context.device = m_device.Get();
        m_context.storage = m_dStorage.Get();

        for(size_t i = 0; i < m_context.descriptorPool.size(); ++i)
        {
            m_context.descriptorPool[i] = std::make_unique<DescriptorHeapAllocator>();
            m_context.descriptorPool[i]->create(m_device.Get(), c_DescriptorPoolSize[i].type, c_DescriptorPoolSize[i].count);
        }

        m_context.descriptorPoolCpuOnly = std::make_unique<DescriptorHeapAllocator>();
        m_context.descriptorPoolCpuOnly->create(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, false);

        m_queues[int(QueueType::Graphics)] = std::make_unique<Queue>(m_context, QueueType::Graphics);
        m_queues[int(QueueType::Transfer)] = std::make_unique<Queue>(m_context, QueueType::Transfer);

        m_context.queue = m_queues[int(QueueType::Graphics)]->m_commandQueue.Get();

        m_storage = std::make_shared<Storage>(m_context);
        m_storage->m_device = this;
    }

    void Buffer::uploadFromMemory(const void* src, uint32_t byteSize) const
    {
        void* pMappedData;
        if (staging() && sizeBytes() >= byteSize && SUCCEEDED(handle->Map(0, nullptr, &pMappedData)))
        {
            memcpy(pMappedData, src, byteSize);
            handle->Unmap(0, nullptr);
        }
        else
            log::error("Failed to upload to buffer");
    }

    BufferPtr Device::createBuffer(uint32_t byteSize, bool staging)
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

        HRESULT hr = m_allocator->CreateResource(
                &buffer->allocDesc,
                &buffer->desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                &buffer->allocation,
                IID_NULL, nullptr);

        if(FAILED(hr))
            log::error("Failed to allocate buffer");
        else
            buffer->handle = buffer->allocation->GetResource();

        return buffer;
    }

    BufferPtr Device::createBuffer(const BufferDesc& desc)
    {
        auto buffer = std::make_shared<Buffer>();

        buffer->isCBV = desc.isConstantBuffer;
        buffer->desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer->desc.Alignment = desc.alignment;
        buffer->desc.Width = desc.byteSize;
        buffer->desc.Height = 1;
        buffer->desc.DepthOrArraySize = 1;
        buffer->desc.MipLevels = 1;
        buffer->desc.Format = DXGI_FORMAT_UNKNOWN;
        buffer->desc.SampleDesc.Count = 1;
        buffer->desc.SampleDesc.Quality = 0;
        buffer->desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        buffer->desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        buffer->state = Common;
        if(desc.isUAV)
        {
            buffer->desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            buffer->clearCpuHandle = m_context.descriptorPoolCpuOnly->allocate(1);
            buffer->clearGpuHandle = m_context.descriptorPool[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->allocate(1);
        }
        if(desc.isConstantBuffer)
            buffer->desc.Width = align(desc.byteSize, 256u); // CB size is required to be 256-byte aligned.

        buffer->allocDesc.HeapType = desc.isStaging ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;

        HRESULT hr = m_allocator->CreateResource(
                &buffer->allocDesc,
                &buffer->desc,
            D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                &buffer->allocation,
                IID_NULL, nullptr);

        if(FAILED(hr))
            log::error("Failed to allocate buffer");
        else
            buffer->handle = buffer->allocation->GetResource();

        return buffer;
    }

    BufferPtr Device::createHostBuffer(uint32_t byteSize)
    {
        return createBuffer(byteSize, true);
    }

    TexturePtr Device::createTexture(const TextureDesc& desc)
    {
        auto texture = std::make_shared<Texture>();
        populateTexture(texture, desc);

        if(desc.isTiled)
        {
            UINT updatedRegions = 0;
            std::vector<D3D12_TILED_RESOURCE_COORDINATE> startCoordinates;
            std::vector<D3D12_TILE_REGION_SIZE> regionSizes;
            std::vector<D3D12_TILE_RANGE_FLAGS> rangeFlags;
            std::vector<UINT> heapRangeStartOffsets;
            std::vector<UINT> rangeTileCounts;

            UINT numTiles = 0;
            D3D12_TILE_SHAPE tileShape = {};
            D3D12_PACKED_MIP_INFO packedMipInfo;
            UINT subresourceCount = texture->desc.MipLevels;
            std::vector<D3D12_SUBRESOURCE_TILING> tilings(subresourceCount);
            m_device->GetResourceTiling(texture->handle, &numTiles, &packedMipInfo, &tileShape, &subresourceCount, 0, &tilings[0]);

            for(size_t i = 0; i < packedMipInfo.NumStandardMips; ++i)
            {
                D3D12_TILED_RESOURCE_COORDINATE coord;
                coord.X = 0;
                coord.Y = 0;
                coord.Z = 0;
                coord.Subresource = i;
                startCoordinates.push_back(coord);
                D3D12_TILE_REGION_SIZE size;
                size.NumTiles = tilings[i].WidthInTiles * tilings[i].HeightInTiles;
                size.UseBox = true;
                size.Width = tilings[i].WidthInTiles;
                size.Height = tilings[i].HeightInTiles;
                size.Depth = tilings[i].DepthInTiles;
                regionSizes.push_back(size);
                updatedRegions++;
            }

            startCoordinates.push_back({0, 0, 0, 3});
            regionSizes.push_back({1, false, 1, 1, 1});
            updatedRegions++;

            rangeFlags.emplace_back(D3D12_TILE_RANGE_FLAG_NONE);
            heapRangeStartOffsets.emplace_back(0);
            rangeTileCounts.emplace_back(22);

            m_context.queue->UpdateTileMappings(
                    texture->handle,
                    updatedRegions,
                    &startCoordinates[0],
                    &regionSizes[0],
                    texture->dHeap.Get(),
                    1,
                    rangeFlags.data(),
                    heapRangeStartOffsets.data(),
                    rangeTileCounts.data(),
                    D3D12_TILE_MAPPING_FLAG_NONE
            );
        }

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
}