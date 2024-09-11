//
// Created by loulfy on 31/12/2023.
//

#pragma once

#include "log/log.hpp"

#include <map>

namespace ler::sys
{
class VariableSizeAllocator
{
  public:
    using OffsetType = size_t;

    static constexpr OffsetType InvalidOffset = std::numeric_limits<OffsetType>::max();

    void reset(OffsetType maxSize);
    OffsetType allocate(OffsetType size);
    void free(OffsetType offset, OffsetType size);

  private:
    struct FreeBlockInfo;

    // Type of the map that keeps memory blocks sorted by their offsets
    using TFreeBlocksByOffsetMap = std::map<OffsetType, FreeBlockInfo, std::less<>>;

    // Type of the map that keeps memory blocks sorted by their sizes
    using TFreeBlocksBySizeMap = std::multimap<OffsetType, TFreeBlocksByOffsetMap::iterator, std::less<>>;

    struct FreeBlockInfo
    {
        // Block size (no reserved space for the size of allocation)
        OffsetType size;

        // Iterator referencing this block in the multimap sorted by the block size
        TFreeBlocksBySizeMap::iterator orderBySizeIt;

        explicit FreeBlockInfo(OffsetType size) : size(size)
        {
        }
    };

    void addNewBlock(OffsetType Offset, OffsetType Size);

    OffsetType m_maxSize = 0u;
    OffsetType m_freeSize = 0u;
    TFreeBlocksByOffsetMap m_freeBlocksByOffset;
    TFreeBlocksBySizeMap m_freeBlocksBySize;
};
} // namespace ler::sys
