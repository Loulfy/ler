//
// Created by loulfy on 29/12/2023.
//

#include "rhi/d3d12.hpp"

namespace ler::rhi::d3d12
{
    DescriptorHeapAllocation::DescriptorHeapAllocation(DescriptorHeapAllocator* pAllocator, ID3D12DescriptorHeap* pHeap,
                                                       D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle,
                                                       D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle, uint32_t descriptorSize,
                                                       uint32_t NHandles) noexcept:
            m_firstCpuHandle{CpuHandle},
            m_firstGpuHandle{GpuHandle},
            m_numHandles{NHandles},
            m_pAllocator{pAllocator},
            m_pDescriptorHeap{pHeap},
            m_descriptorSize{descriptorSize}
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

    DescriptorHeapAllocation::DescriptorHeapAllocation(DescriptorHeapAllocation&& alloc) noexcept:
            m_firstCpuHandle{alloc.m_firstCpuHandle},
            m_firstGpuHandle{alloc.m_firstGpuHandle},
            m_numHandles{alloc.m_numHandles},
            m_pAllocator{alloc.m_pAllocator},
            m_pDescriptorHeap{alloc.m_pDescriptorHeap},
            m_descriptorSize{alloc.m_descriptorSize}
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

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapAllocation:: getCpuHandle(uint32_t offset) const
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

    void DescriptorHeapAllocator::create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shaderVisible)
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

        return {this, m_heap.Get(), CPUHandle, GPUHandle, m_descriptorSize, count};
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
}