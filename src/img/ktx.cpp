//
// Created by loulfy on 07/01/2024.
//

#include "ktx.hpp"
#include <cmath>

namespace ler::img
{
rhi::Format KtxTexture::vkFormatToRhiFormat(uint32_t vkFormat)
{
    switch (vkFormat)
    {
    default:
        throw std::runtime_error("Not Implemented");
    case 0:
        return rhi::Format::UNKNOWN;
    case 37:
        return rhi::Format::RGBA8_UNORM;
    case 133:
        return rhi::Format::BC1_UNORM;
    case 134:
        return rhi::Format::BC1_UNORM_SRGB;
    case 135:
        return rhi::Format::BC2_UNORM;
    case 136:
        return rhi::Format::BC2_UNORM_SRGB;
    case 137:
        return rhi::Format::BC3_UNORM;
    case 138:
        return rhi::Format::BC3_UNORM_SRGB;
    case 139:
        return rhi::Format::BC4_UNORM;
    case 140:
        return rhi::Format::BC4_SNORM;
    case 141:
        return rhi::Format::BC5_UNORM;
    case 142:
        return rhi::Format::BC5_SNORM;
    case 143:
        return rhi::Format::BC6H_UFLOAT;
    case 144:
        return rhi::Format::BC6H_SFLOAT;
    case 145:
        return rhi::Format::BC7_UNORM;
    case 146:
        return rhi::Format::BC7_UNORM_SRGB;
    }
}

void KtxTexture::init()
{
    pixelDepth = std::max(pixelDepth, 1u);
    formatSize = formatToBlockInfo(img::KtxTexture::vkFormatToRhiFormat(vkFormat));
}

void KtxTexture::open(const fs::path &path)
{
    std::ifstream f(path, std::ios::binary);
    f >> *this;
    f.close();
}

[[nodiscard]] uint32_t KtxTexture::getDataSize() const
{
    uint32_t dataSize = 0u;
    for (auto &level : levels())
        dataSize += level.byteLength;
    return dataSize;
}

[[nodiscard]] uint32_t KtxTexture::getRowPitch(uint32_t level) const
{
    const uint32_t blockCount = std::max(1u, (pixelWidth / formatSize.blockWidth) >> level);
    uint32_t pitch = blockCount * formatSize.blockSizeByte;
    // Pad ROW
    auto rowPadding = static_cast<uint32_t>(4 * std::ceil((float)pitch / 4.f) - (float)pitch);
    pitch += rowPadding;

    return pitch;
}

rhi::Format KtxTexture::getFormat() const
{
    return vkFormatToRhiFormat(vkFormat);
}

std::span<const ITexture::LevelIndexEntry> KtxTexture::levels() const
{
    return {levelIndex.data(), levelCount};
}

uint64_t KtxTexture::headOffset() const
{
    return levelIndex[levelCount - 1].byteOffset;
}

rhi::TextureDesc KtxTexture::desc() const
{
    rhi::TextureDesc desc;
    desc.depth = pixelDepth;
    desc.width = pixelWidth;
    desc.height = pixelHeight;
    desc.format = getFormat();
    desc.mipLevels = levelCount;
    return desc;
}

void KtxTexture::initFromBuffer(void* data)
{
    memcpy(identifier, data, kBytesToRead);
    init();
}

const ITexture::LevelIndexEntry &KtxTexture::tail() const
{
    return levelIndex[levelCount - 1];
}

std::istream &operator>>(std::istream &in, KtxTexture &ktx)
{
    in.read(reinterpret_cast<char *>(ktx.identifier), 12);
    if (memcmp(ktx.identifier, KtxTexture::ktx2_ident_ref, 12) == 0)
        in.read(reinterpret_cast<char *>(&ktx.vkFormat), 68);
    else
        throw std::runtime_error("Not KTX 2!");

    uint32_t numLevels = std::max(1u, ktx.levelCount);
    uint32_t levelIndexSize = sizeof(KtxLevelIndexEntry) * numLevels;
    in.read(reinterpret_cast<char *>(ktx.levelIndex.data()), levelIndexSize);

    ktx.formatSize = formatToBlockInfo(KtxTexture::vkFormatToRhiFormat(ktx.vkFormat));
    return in;
}
} // namespace ler::img