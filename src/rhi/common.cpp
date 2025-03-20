//
// Created by Loulfy on 08/03/2025.
//

#include "rhi.hpp"

namespace ler::rhi
{
ScratchBuffer::ScratchBuffer(IDevice* device)
{
    m_staging = device->createBuffer(m_capacity, true);
}

uint64_t ScratchBuffer::allocate(uint32_t sizeInByte)
{
    uint64_t offset = m_size.fetch_add(sizeInByte);
    assert(m_size < m_capacity);
    return offset;
}

void ScratchBuffer::reset()
{
    m_size.store(0);
}

const BufferPtr& ScratchBuffer::getBuffer() const
{
    return m_staging;
}
} // namespace ler::rhi