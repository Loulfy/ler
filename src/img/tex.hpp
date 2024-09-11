//
// Created by loulfy on 20/02/2024.
//

#pragma once

#include "rhi/rhi.hpp"

namespace ler::img
{
class ITexture
{
  public:
    struct LevelIndexEntry
    {
        uint64_t byteOffset = 0u; /*!< Offset of level from start of file. */
        uint64_t byteLength = 0u; /*!< Number of bytes of compressed image data in the level. */
        uint64_t uncompressedByteLength = 0u;
        /*!< Number of bytes of uncompressed image data in the level. */
    };

    virtual ~ITexture() = default;
    virtual void init() = 0;
    [[nodiscard]] virtual uint32_t getDataSize() const = 0;
    [[nodiscard]] virtual uint32_t getRowPitch(uint32_t level) const = 0;
    [[nodiscard]] virtual rhi::Format getFormat() const = 0;
    [[nodiscard]] virtual std::span<const LevelIndexEntry> levels() const = 0;
    [[nodiscard]] virtual uint32_t headOffset() const = 0;
    [[nodiscard]] virtual rhi::TextureDesc desc() const = 0;
    virtual void initFromBuffer(void* data) = 0;
};
} // namespace ler::img
