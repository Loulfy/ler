//
// Created by loulfy on 07/01/2024.
//

#pragma once

#include "tex.hpp"
#include <algorithm>

namespace ler::img
{
#define KTX2_IDENTIFIER_REF                                                                                            \
    {                                                                                                                  \
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A                                         \
    }

/**
 * @internal
 * @~English
 * @brief 32-bit KTX 2 index entry.
 */
struct KtxIndexEntry32
{
    uint32_t byteOffset = 0u; /*!< Offset of item from start of file. */
    uint32_t byteLength = 0u; /*!< Number of bytes of data in the item. */
};
/**
 * @internal
 * @~English
 * @brief 64-bit KTX 2 index entry.
 */
struct KtxIndexEntry64
{
    uint64_t byteOffset = 0u; /*!< Offset of item from start of file. */
    uint64_t byteLength = 0u; /*!< Number of bytes of data in the item. */
};
/**
 * @internal
 * @~English
 * @brief KTX 2 file header.
 *
 * See the KTX 2 specification for descriptions.
 */
struct KTX_header2
{
    uint8_t identifier[12] = {};
    uint32_t vkFormat = 0u;
    uint32_t typeSize = 1u;
    uint32_t pixelWidth = 0u;
    uint32_t pixelHeight = 0u;
    uint32_t pixelDepth = 0u;
    uint32_t layerCount = 0u;
    uint32_t faceCount = 0u;
    uint32_t levelCount = 0u;
    uint32_t supercompressionScheme = 0u;
    KtxIndexEntry32 dataFormatDescriptor;
    KtxIndexEntry32 keyValueData;
    KtxIndexEntry64 supercompressionGlobalData;
};
/**
 * @internal
 * @~English
 * @brief KTX 2 level index entry.
 */
struct KtxLevelIndexEntry
{
    uint64_t byteOffset = 0u; /*!< Offset of level from start of file. */
    uint64_t byteLength = 0u;
    /*!< Number of bytes of compressed image data in the level. */
    uint64_t uncompressedByteLength = 0u;
    /*!< Number of bytes of uncompressed image data in the level. */
};

struct KtxTexture : public ITexture
{
    uint8_t identifier[12] = {};
    uint32_t vkFormat = 0u;
    uint32_t typeSize = 1u;
    uint32_t pixelWidth = 0u;
    uint32_t pixelHeight = 0u;
    uint32_t pixelDepth = 1u;
    uint32_t layerCount = 0u;
    uint32_t faceCount = 0u;
    uint32_t levelCount = 0u;
    uint32_t supercompressionScheme = 0u;
    KtxIndexEntry32 dataFormatDescriptor;
    KtxIndexEntry32 keyValueData;
    KtxIndexEntry64 supercompressionGlobalData;
    std::array<LevelIndexEntry, 12> levelIndex;
    rhi::FormatBlockInfo formatSize;

    static constexpr uint8_t ktx2_ident_ref[12] = KTX2_IDENTIFIER_REF;

    void init() override;
    void open(const fs::path &path);
    [[nodiscard]] uint32_t getDataSize() const override;
    [[nodiscard]] uint32_t getRowPitch(uint32_t level) const override;
    [[nodiscard]] rhi::Format getFormat() const override;
    [[nodiscard]] std::span<const LevelIndexEntry> levels() const override;
    [[nodiscard]] uint32_t headOffset() const override;
    [[nodiscard]] rhi::TextureDesc desc() const override;
    [[nodiscard]] const LevelIndexEntry &tail() const;
    void initFromBuffer(void* data) override;

    friend std::istream &operator>>(std::istream &in, KtxTexture &ktx);
    static constexpr uint32_t kBytesToRead = sizeof(KTX_header2) + sizeof(ITexture::LevelIndexEntry) * 12u;

  private:
    static rhi::Format vkFormatToRhiFormat(uint32_t vkFormat);
};
} // namespace ler::img
