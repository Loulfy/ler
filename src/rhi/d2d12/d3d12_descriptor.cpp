//
// Created by loulfy on 29/12/2023.
//

#include "rhi/d3d12.hpp"

namespace ler::rhi::d3d12
{
DescriptorHeapAllocation::DescriptorHeapAllocation(DescriptorHeapAllocator* pAllocator, ID3D12DescriptorHeap* pHeap,
                                                   D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle,
                                                   D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle, uint32_t descriptorSize,
                                                   uint32_t NHandles) noexcept
    : m_firstCpuHandle{ CpuHandle }, m_firstGpuHandle{ GpuHandle }, m_numHandles{ NHandles },
      m_pAllocator{ pAllocator }, m_pDescriptorHeap{ pHeap }, m_descriptorSize{ descriptorSize }
{
}

void DescriptorHeapAllocation::reset()
{
    m_firstCpuHandle.ptr = 0;
    m_firstGpuHandle.ptr = 0;
    m_pAllocator = nullptr;
    m_pDescriptorHeap = nullptr;
    m_numHandles = 0;
    m_descriptorSize = 0;
}

DescriptorHeapAllocation::~DescriptorHeapAllocation()
{
    if (!isNull() && m_pAllocator)
        m_pAllocator->free(std::move(*this));
}

DescriptorHeapAllocation::DescriptorHeapAllocation(DescriptorHeapAllocation&& alloc) noexcept
    : m_firstCpuHandle{ alloc.m_firstCpuHandle }, m_firstGpuHandle{ alloc.m_firstGpuHandle },
      m_numHandles{ alloc.m_numHandles }, m_pAllocator{ alloc.m_pAllocator },
      m_pDescriptorHeap{ alloc.m_pDescriptorHeap }, m_descriptorSize{ alloc.m_descriptorSize }
{
    alloc.reset();
}

DescriptorHeapAllocation& DescriptorHeapAllocation::operator=(DescriptorHeapAllocation&& alloc) noexcept
{
    m_firstCpuHandle = alloc.m_firstCpuHandle;
    m_firstGpuHandle = alloc.m_firstGpuHandle;
    m_numHandles = alloc.m_numHandles;
    m_pAllocator = alloc.m_pAllocator;
    m_pDescriptorHeap = alloc.m_pDescriptorHeap;
    m_descriptorSize = alloc.m_descriptorSize;
    alloc.reset();
    return *this;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapAllocation::getCpuHandle(uint32_t offset) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_firstCpuHandle;
    if (offset != 0)
        CPUHandle.ptr += m_descriptorSize * offset;
    return CPUHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapAllocation::getGpuHandle(uint32_t offset) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = m_firstGpuHandle;
    if (offset != 0)
        GPUHandle.ptr += m_descriptorSize * offset;
    return GPUHandle;
}

void DescriptorHeapAllocator::create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count,
                                     bool shaderVisible)
{
    desc.Type = type;
    desc.NumDescriptors = count;
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
        desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
    m_freeBlockManager.reset(count);
}

DescriptorHeapAllocation DescriptorHeapAllocator::allocate(uint32_t count)
{
    // Use variable-size GPU allocations manager to allocate the requested number of descriptors
    auto DescriptorHandleOffset = m_freeBlockManager.allocate(count);
    if (DescriptorHandleOffset == sys::VariableSizeAllocator::InvalidOffset || count == 0)
        return {};

    // Compute the first CPU and GPU descriptor handles in the allocation by
    // offsetting the first CPU and GPU descriptor handle in the range
    auto CPUHandle = m_heap->GetCPUDescriptorHandleForHeapStart();
    CPUHandle.ptr += DescriptorHandleOffset * m_descriptorSize;

    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = {};
    if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        GPUHandle = m_heap->GetGPUDescriptorHandleForHeapStart();
        GPUHandle.ptr += DescriptorHandleOffset * m_descriptorSize;
    }

    return { this, m_heap.Get(), CPUHandle, GPUHandle, m_descriptorSize, count };
}

void DescriptorHeapAllocator::free(DescriptorHeapAllocation&& alloc)
{
    auto DescriptorOffset =
        (alloc.getCpuHandle().ptr - m_heap->GetCPUDescriptorHandleForHeapStart().ptr) / m_descriptorSize;
    m_freeBlockManager.free(DescriptorOffset, alloc.getNumHandles());
    // Clear the allocation
    alloc.reset();
}

/*std::vector<ID3D12DescriptorHeap*> DescriptorSet::heap() const
{
    std::vector<ID3D12DescriptorHeap*> heaps;
    for (auto& alloc: tables)
    {
        if (!alloc.isNull())
            heaps.emplace_back(alloc.heap());
    }
    return heaps;
}*/

BindlessTable::BindlessTable(const D3D12Context& context, uint32_t count) : m_context(context)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = count;
    //m_context.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heapResCpu));
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_context.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heapRes));
    m_descriptorSize = m_context.device->GetDescriptorHandleIncrementSize(desc.Type);

    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    m_context.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heapSamp));
}

void BindlessTable::setSampler(const SamplerPtr& sampler, uint32_t slot)
{
    auto* samp = checked_cast<Sampler*>(sampler.get());

    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_heapSamp->GetCPUDescriptorHandleForHeapStart();
    CPUHandle.ptr += slot * m_context.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    m_context.device->CreateSampler(&samp->desc, CPUHandle);
}

bool BindlessTable::visitTexture(const TexturePtr& texture, uint32_t slot)
{
    auto* image = checked_cast<Texture*>(texture.get());

    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_heapRes->GetCPUDescriptorHandleForHeapStart();
    CPUHandle.ptr += slot * m_descriptorSize;

    // Describe and create a SRV for the resource.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = image->desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = image->desc.MipLevels;

    m_context.device->CreateShaderResourceView(image->handle, &srvDesc, CPUHandle);
    return true;
}

bool BindlessTable::visitBuffer(const BufferPtr& buffer, uint32_t slot)
{
    auto* buff = checked_cast<Buffer*>(buffer.get());

    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_heapRes->GetCPUDescriptorHandleForHeapStart();
    CPUHandle.ptr += slot * m_descriptorSize;

    uint32_t stride = buff->stride;
    if(buff->isCBV)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = buff->handle->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = buff->sizeBytes();

        m_context.device->CreateConstantBufferView(&cbvDesc, CPUHandle);
    }
    else if(buff->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        if(stride > 0)
        {
            uavDesc.Buffer.StructureByteStride = stride;
            uavDesc.Buffer.NumElements = buff->sizeBytes()/stride;
        }
        else
        {
            uavDesc.Format = buff->format;
            uavDesc.Buffer.NumElements = buff->sizeBytes()/4u;
        }

        //D3D12_CPU_DESCRIPTOR_HANDLE CPUOnlyHandle = m_heapResCpu->GetCPUDescriptorHandleForHeapStart();
        //CPUOnlyHandle.ptr += slot * m_descriptorSize;

        m_context.device->CreateUnorderedAccessView(buff->handle, nullptr, &uavDesc, CPUHandle);
        //m_context.device->CopyDescriptorsSimple(1, CPUHandle, CPUOnlyHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    else
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.StructureByteStride = stride;
        srvDesc.Buffer.NumElements = buff->sizeBytes()/stride;

        m_context.device->CreateShaderResourceView(buff->handle, &srvDesc, CPUHandle);
    }
    return true;
}

BindlessTablePtr Device::createBindlessTable(uint32_t count)
{
    return std::make_shared<BindlessTable>(m_context, count);
}
} // namespace ler::rhi::d3d12