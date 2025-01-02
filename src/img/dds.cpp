//
// Created by loulfy on 20/02/2024.
//

#include "dds.hpp"

namespace ler::img
{
rhi::Format DdsTexture::dxgiFormatToRhiFormat(int32_t dxgiFormat)
{
    switch (dxgiFormat)
    {
    default:
        throw std::runtime_error("Not Implemented");
    case 0:
        return rhi::Format::UNKNOWN;
    case 28:
        return rhi::Format::RGBA8_UNORM;
    case 71:
        return rhi::Format::BC1_UNORM;
    case 72:
        return rhi::Format::BC1_UNORM_SRGB;
    case 74:
        return rhi::Format::BC2_UNORM;
    case 75:
        return rhi::Format::BC2_UNORM_SRGB;
    case 77:
        return rhi::Format::BC3_UNORM;
    case 78:
        return rhi::Format::BC3_UNORM_SRGB;
    case 80:
        return rhi::Format::BC4_UNORM;
    case 81:
        return rhi::Format::BC4_SNORM;
    case 83:
        return rhi::Format::BC5_UNORM;
    case 84:
        return rhi::Format::BC5_SNORM;
    case 95:
        return rhi::Format::BC6H_UFLOAT;
    case 96:
        return rhi::Format::BC6H_SFLOAT;
    case 98:
        return rhi::Format::BC7_UNORM;
    case 99:
        return rhi::Format::BC7_UNORM_SRGB;
    }
}

template <typename T> constexpr bool hasBit(T value, T bit)
{
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(bit)) == static_cast<uint32_t>(bit);
}

int32_t DdsTexture::getFormatInfo(const FileHeader* header)
{
    const auto& pf = header->pixelFormat;
    if (hasBit(pf.flags, PixelFormatFlags::FourCC))
    {
        switch (pf.fourCC)
        {
            // clang-format off
                case DXT1:            return 71;
                case DXT2: case DXT3: return 74;
                case DXT4: case DXT5: return 77;
                case ATI1: case BC4U: return 80;
                case BC4S:            return 81;
                case ATI2: case BC5U: return 83;
                case BC5S:            return 84;
                default:              return 0;
            // clang-format on
        }
    }

    // TODO: Write more of this bitmask stuff to determine formats.
    if (hasBit(pf.flags, PixelFormatFlags::RGBA))
    {
        switch (pf.bitCount)
        {
        case 32: {
            if (pf.rBitMask == 0xFF && pf.gBitMask == 0xFF00 && pf.bBitMask == 0xFF0000 && pf.aBitMask == 0xFF000000)
                return 28;
            break;
        default:
            return 0;
        }
        }
    }

    return 0;
}

uint32_t DdsTexture::computeMipmapSize(int32_t dxgiFormat, uint32_t width, uint32_t height)
{
    rhi::Format format = dxgiFormatToRhiFormat(dxgiFormat);
    rhi::FormatBlockInfo formatSize = rhi::formatToBlockInfo(format);

    // Instead of checking each format enum each we'll check for the range in
    // which the BCn compressed formats are.
    if ((dxgiFormat >= 70 && dxgiFormat <= 84) || (dxgiFormat >= 94 && dxgiFormat <= 99))
    {
        auto pitch = std::max(1u, (width + 3) / 4) * formatSize.blockSizeByte;
        return pitch * std::max(1u, (height + 3) / 4);
    }

    // These formats are special
    // DXGI_FORMAT_R8G8_B8G8_UNORM
    // DXGI_FORMAT_G8R8_G8B8_UNORM
    if (dxgiFormat == 68 || dxgiFormat == 69)
    {
        return ((width + 1) >> 1) * 4 * height;
    }

    uint32_t bitsPerPixel = formatSize.blockSizeByte * 8;
    return std::max(1u, static_cast<uint32_t>((width * bitsPerPixel + 7) / 8)) * height;
}

void DdsTexture::open(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    f.read(reinterpret_cast<char*>(&ddsMagic), 148);
    f.close();
}

void DdsTexture::init()
{
    assert(ddsMagic == DdsMagicNumber::DDS);

    uint32_t ptr = sizeof(uint32_t); // ddsMagic
    ptr += sizeof(FileHeader);

    bool hasAdditionalHeader = false;
    if (hasBit(header.pixelFormat.flags, PixelFormatFlags::FourCC) &&
        hasBit(header.pixelFormat.fourCC, static_cast<uint32_t>(DX10)))
    {
        ptr += sizeof(Dx10Header);
        hasAdditionalHeader = true;
    }

    header.depth = std::max(header.depth, 1u);

    if (hasAdditionalHeader)
        format = additionalHeader.dxgiFormat;
    else
        format = getFormatInfo(&header);

    rhi::Format rhiFormat = dxgiFormatToRhiFormat(format);
    formatSize = formatToBlockInfo(rhiFormat);

    // arraySize is populated with the additional DX10 header.
    uint64_t totalSize = 0;
    for (uint32_t i = 0; i < 1; ++i)
    {
        header.mipmapCount = std::max(header.mipmapCount, 1u);
        auto width = header.width;
        auto height = header.height;

        for (uint32_t mip = 0; mip < header.mipmapCount && width != 0; ++mip)
        {
            const uint32_t size = computeMipmapSize(format, width, height);
            totalSize += static_cast<uint64_t>(size);

            levelIndex[mip].byteLength = size;
            levelIndex[mip].byteOffset = ptr;
            ptr += size;

            width = std::max(width / 2, 1u);
            height = std::max(height / 2, 1u);
        }
    }
}

[[nodiscard]] uint32_t DdsTexture::getDataSize() const
{
    uint32_t dataSize = 0u;
    for (auto& level : levels())
        dataSize += level.byteLength;
    return dataSize;
}

[[nodiscard]] uint32_t DdsTexture::getRowPitch(uint32_t level) const
{

    const uint32_t blockCount = std::max(1u, (header.width / formatSize.blockWidth) >> level);
    uint32_t pitch = blockCount * formatSize.blockSizeByte;
    // Pad ROW
    auto rowPadding = static_cast<uint32_t>(4 * std::ceil((float)pitch / 4.f) - (float)pitch);
    pitch += rowPadding;

    /*if ((format >= 70 && format <= 84) ||
        (format >= 94 && format <= 99))
    {
        auto pitchA = std::max(1u, (header.width + 3) / 4) * formatSize.blockSizeByte;
        return pitchA * std::max(1u, (header.height + 3) / 4);
    }*/

    return pitch;
}

rhi::Format DdsTexture::getFormat() const
{
    return dxgiFormatToRhiFormat(format);
}

std::span<const ITexture::LevelIndexEntry> DdsTexture::levels() const
{
    return { levelIndex.data(), header.mipmapCount };
}

uint64_t DdsTexture::headOffset() const
{
    return levelIndex[0].byteOffset;
}

rhi::TextureDesc DdsTexture::desc() const
{
    rhi::TextureDesc desc;
    desc.depth = header.depth;
    desc.width = std::max(header.width, formatSize.blockWidth);
    desc.height = std::max(header.height, formatSize.blockHeight);
    desc.format = getFormat();
    desc.mipLevels = header.mipmapCount;
    return desc;
}

void DdsTexture::initFromBuffer(void* data)
{
    memcpy(&ddsMagic, data, kBytesToRead);
    init();
}

const ITexture::LevelIndexEntry& DdsTexture::tail() const
{
    return levelIndex[0];
}
} // namespace ler::img