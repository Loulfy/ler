//
// Created by loulfy on 17/02/2024.
//

#pragma once

#include "tex.hpp"
#include <algorithm>

namespace ler::img
{
#define MAKE_FOUR_CHARACTER_CODE(char1, char2, char3, char4)                                                           \
    static_cast<uint32_t>(char1) | (static_cast<uint32_t>(char2) << 8) | (static_cast<uint32_t>(char3) << 16) |        \
        (static_cast<uint32_t>(char4) << 24)

enum ResourceDimension
{
    Unknown,
    Buffer,
    Texture1D,
    Texture2D,
    Texture3D
};

enum ReadResult
{
    Success = 0,
    Failure = -1,
    UnsupportedFormat = -2,
    NoDx10Header = -3,
    InvalidSize = -4,
};

enum DdsMagicNumber
{
    DDS = MAKE_FOUR_CHARACTER_CODE('D', 'D', 'S', ' '),

    DXT1 = MAKE_FOUR_CHARACTER_CODE('D', 'X', 'T', '1'), // BC1_UNORM
    DXT2 = MAKE_FOUR_CHARACTER_CODE('D', 'X', 'T', '2'), // BC2_UNORM
    DXT3 = MAKE_FOUR_CHARACTER_CODE('D', 'X', 'T', '3'), // BC2_UNORM
    DXT4 = MAKE_FOUR_CHARACTER_CODE('D', 'X', 'T', '4'), // BC3_UNORM
    DXT5 = MAKE_FOUR_CHARACTER_CODE('D', 'X', 'T', '5'), // BC3_UNORM
    ATI1 = MAKE_FOUR_CHARACTER_CODE('A', 'T', 'I', '1'), // BC4_UNORM
    BC4U = MAKE_FOUR_CHARACTER_CODE('B', 'C', '4', 'U'), // BC4_UNORM
    BC4S = MAKE_FOUR_CHARACTER_CODE('B', 'C', '4', 'S'), // BC4_SNORM
    ATI2 = MAKE_FOUR_CHARACTER_CODE('A', 'T', 'I', '2'), // BC5_UNORM
    BC5U = MAKE_FOUR_CHARACTER_CODE('B', 'C', '5', 'U'), // BC5_UNORM
    BC5S = MAKE_FOUR_CHARACTER_CODE('B', 'C', '5', 'S'), // BC5_SNORM
    RGBG = MAKE_FOUR_CHARACTER_CODE('R', 'G', 'B', 'G'), // R8G8_B8G8_UNORM
    GRBG = MAKE_FOUR_CHARACTER_CODE('G', 'R', 'B', 'G'), // G8R8_G8B8_UNORM
    YUY2 = MAKE_FOUR_CHARACTER_CODE('Y', 'U', 'Y', '2'), // YUY2
    UYVY = MAKE_FOUR_CHARACTER_CODE('U', 'Y', 'V', 'Y'),

    DX10 = MAKE_FOUR_CHARACTER_CODE('D', 'X', '1', '0'), // Any DXGI format
};

enum class PixelFormatFlags : uint32_t
{
    AlphaPixels = 0x1,
    Alpha = 0x2,
    FourCC = 0x4,
    PAL8 = 0x20,
    RGB = 0x40,
    RGBA = RGB | AlphaPixels,
    YUV = 0x200,
    Luminance = 0x20000,
    LuminanceA = Luminance | AlphaPixels,
};

[[nodiscard]] inline PixelFormatFlags operator&(PixelFormatFlags a, PixelFormatFlags b)
{
    return static_cast<PixelFormatFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

struct FilePixelFormat
{
    uint32_t size = 0u;
    PixelFormatFlags flags = PixelFormatFlags::FourCC;
    uint32_t fourCC = 0u;
    uint32_t bitCount = 0u;
    uint32_t rBitMask = 0u;
    uint32_t gBitMask = 0u;
    uint32_t bBitMask = 0u;
    uint32_t aBitMask = 0u;
};

enum HeaderFlags : uint32_t
{
    Caps = 0x1,
    Height = 0x2,
    Width = 0x4,
    Pitch = 0x8,
    PixelFormat = 0x1000,
    Texture = Caps | Height | Width | PixelFormat,
    Mipmap = 0x20000,
    Volume = 0x800000,
    LinearSize = 0x00080000,
};

enum Caps2Flags : uint32_t
{
    Cubemap = 0x200,
};

struct FileHeader
{
    uint32_t size = 0u;
    HeaderFlags flags = Caps;
    uint32_t height = 0u;
    uint32_t width = 0u;
    uint32_t pitch = 0u;
    uint32_t depth = 1u;
    uint32_t mipmapCount = 1u;
    uint32_t reserved[11] = {};
    FilePixelFormat pixelFormat;
    uint32_t caps1 = 0u;
    uint32_t caps2 = 0u;
    uint32_t caps3 = 0u;
    uint32_t caps4 = 0u;
    uint32_t reserved2 = 0u;

    [[nodiscard]] bool hasAlphaFlag() const;
};

static_assert(sizeof(FileHeader) == 124, "DDS Header size mismatch. Must be 124 bytes.");

inline bool FileHeader::hasAlphaFlag() const
{
    return (pixelFormat.flags & PixelFormatFlags::AlphaPixels) == PixelFormatFlags::AlphaPixels;
}

/** An additional header for DX10 */
struct Dx10Header
{
    int32_t dxgiFormat = 0;
    ResourceDimension resourceDimension = Unknown;
    uint32_t miscFlags = 0u;
    uint32_t arraySize = 0u;
    uint32_t miscFlags2 = 0u;
};

struct DdsTexture : public ITexture
{
    uint32_t ddsMagic = 0u;
    FileHeader header;
    Dx10Header additionalHeader;
    std::array<LevelIndexEntry, 12> levelIndex;
    rhi::FormatBlockInfo formatSize;
    int32_t format = 0;

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

    static constexpr uint32_t kBytesToRead = sizeof(FileHeader) + sizeof(Dx10Header) + 4u;

  private:
    static rhi::Format dxgiFormatToRhiFormat(int32_t dxgiFormat);
    // Determine format information
    static int32_t getFormatInfo(const FileHeader *header);
    static uint32_t computeMipmapSize(int32_t dxgiFormat, uint32_t width, uint32_t height);
};
} // namespace ler::img
