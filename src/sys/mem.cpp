//
// Created by loulfy on 31/12/2023.
//

#include "mem.hpp"

namespace ler::sys
{
void VariableSizeAllocator::reset(OffsetType maxSize)
{
    m_maxSize = maxSize;
    m_freeSize = maxSize;
    m_freeBlocksBySize.clear();
    m_freeBlocksByOffset.clear();
    addNewBlock(0, m_freeSize);
}

void VariableSizeAllocator::addNewBlock(OffsetType offset, OffsetType size)
{
    auto NewBlockIt = m_freeBlocksByOffset.emplace(offset, size);
    auto OrderIt = m_freeBlocksBySize.emplace(size, NewBlockIt.first);
    NewBlockIt.first->second.orderBySizeIt = OrderIt;
}

VariableSizeAllocator::OffsetType VariableSizeAllocator::allocate(OffsetType size)
{
    if (m_freeSize < size)
        return InvalidOffset;

    // Get the first block that is large enough to encompass Size bytes
    auto SmallestBlockItIt = m_freeBlocksBySize.lower_bound(size);
    if (SmallestBlockItIt == m_freeBlocksBySize.end())
        return InvalidOffset;

    auto SmallestBlockIt = SmallestBlockItIt->second;
    auto Offset = SmallestBlockIt->first;
    auto NewOffset = Offset + size;
    auto NewSize = SmallestBlockIt->second.size - size;
    m_freeBlocksBySize.erase(SmallestBlockItIt);
    m_freeBlocksByOffset.erase(SmallestBlockIt);
    if (NewSize > 0)
        addNewBlock(NewOffset, NewSize);

    m_freeSize -= size;
    return Offset;
}

void VariableSizeAllocator::free(OffsetType Offset, OffsetType Size)
{
    assert(Offset != InvalidOffset && Offset + Size <= m_maxSize);
    // Find the first element whose offset is greater than the specified offset
    auto NextBlockIt = m_freeBlocksByOffset.upper_bound(Offset);
    auto PrevBlockIt = NextBlockIt;
    if (PrevBlockIt != m_freeBlocksByOffset.begin())
        --PrevBlockIt;
    else
        PrevBlockIt = m_freeBlocksByOffset.end();
    OffsetType NewSize, NewOffset;
    if (PrevBlockIt != m_freeBlocksByOffset.end() && Offset == PrevBlockIt->first + PrevBlockIt->second.size)
    {
        // PrevBlock.Offset           Offset
        // |                          |
        // |<-----PrevBlock.Size----->|<------Size-------->|
        //
        NewSize = PrevBlockIt->second.size + Size;
        NewOffset = PrevBlockIt->first;

        if (NextBlockIt != m_freeBlocksByOffset.end() && Offset + Size == NextBlockIt->first)
        {
            // PrevBlock.Offset           Offset               NextBlock.Offset
            // |                          |                    |
            // |<-----PrevBlock.Size----->|<------Size-------->|<-----NextBlock.Size----->|
            //
            NewSize += NextBlockIt->second.size;
            m_freeBlocksBySize.erase(PrevBlockIt->second.orderBySizeIt);
            m_freeBlocksBySize.erase(NextBlockIt->second.orderBySizeIt);
            // Delete the range of two blocks
            ++NextBlockIt;
            m_freeBlocksByOffset.erase(PrevBlockIt, NextBlockIt);
        }
        else
        {
            // PrevBlock.Offset           Offset                       NextBlock.Offset
            // |                          |                            |
            // |<-----PrevBlock.Size----->|<------Size-------->| ~ ~ ~ |<-----NextBlock.Size----->|
            //
            m_freeBlocksBySize.erase(PrevBlockIt->second.orderBySizeIt);
            m_freeBlocksByOffset.erase(PrevBlockIt);
        }
    }
    else if (NextBlockIt != m_freeBlocksByOffset.end() && Offset + Size == NextBlockIt->first)
    {
        // PrevBlock.Offset                   Offset               NextBlock.Offset
        // |                                  |                    |
        // |<-----PrevBlock.Size----->| ~ ~ ~ |<------Size-------->|<-----NextBlock.Size----->|
        //
        NewSize = Size + NextBlockIt->second.size;
        NewOffset = Offset;
        m_freeBlocksBySize.erase(NextBlockIt->second.orderBySizeIt);
        m_freeBlocksByOffset.erase(NextBlockIt);
    }
    else
    {
        // PrevBlock.Offset                   Offset                       NextBlock.Offset
        // |                                  |                            |
        // |<-----PrevBlock.Size----->| ~ ~ ~ |<------Size-------->| ~ ~ ~ |<-----NextBlock.Size----->|
        //
        NewSize = Size;
        NewOffset = Offset;
    }

    addNewBlock(NewOffset, NewSize);

    m_freeSize += Size;
}
} // namespace ler::sys