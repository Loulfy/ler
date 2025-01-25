//
// Created by Loulfy on 02/01/2025.
//

#include "rhi/metal.hpp"

namespace ler::rhi::metal
{
struct FormatMapping
{
    Format rhiFormat;
    MTL::PixelFormat mtlFormat;
};

static const std::array<FormatMapping, size_t(Format::COUNT)> c_FormatMap = { {
    { Format::UNKNOWN, MTL::PixelFormatInvalid },
    { Format::R8_UINT, MTL::PixelFormatR8Uint },
    { Format::R8_SINT, MTL::PixelFormatR8Sint },
    { Format::R8_UNORM, MTL::PixelFormatR8Unorm },
    { Format::R8_SNORM, MTL::PixelFormatR8Snorm },
    { Format::RG8_UINT, MTL::PixelFormatRG8Uint },
    { Format::RG8_SINT, MTL::PixelFormatRG8Sint },
    { Format::RG8_UNORM, MTL::PixelFormatRG8Unorm },
    { Format::RG8_SNORM, MTL::PixelFormatRG8Snorm },
    { Format::R16_UINT, MTL::PixelFormatR16Uint },
    { Format::R16_SINT, MTL::PixelFormatR16Sint },
    { Format::R16_UNORM, MTL::PixelFormatR16Unorm },
    { Format::R16_SNORM, MTL::PixelFormatR16Snorm },
    { Format::R16_FLOAT, MTL::PixelFormatR16Float },
    { Format::BGRA4_UNORM, MTL::PixelFormatABGR4Unorm },
    { Format::B5G6R5_UNORM, MTL::PixelFormatB5G6R5Unorm },
    { Format::B5G5R5A1_UNORM, MTL::PixelFormatBGR5A1Unorm },
    { Format::RGBA8_UINT, MTL::PixelFormatRGBA8Uint },
    { Format::RGBA8_SINT, MTL::PixelFormatRGBA8Sint },
    { Format::RGBA8_UNORM, MTL::PixelFormatRGBA8Unorm },
    { Format::RGBA8_SNORM, MTL::PixelFormatRGBA8Snorm },
    { Format::BGRA8_UNORM, MTL::PixelFormatBGRA8Unorm },
    { Format::SRGBA8_UNORM, MTL::PixelFormatRGBA8Unorm_sRGB },
    { Format::SBGRA8_UNORM, MTL::PixelFormatBGRA8Unorm_sRGB },
    { Format::R10G10B10A2_UNORM, MTL::PixelFormatRGB10A2Unorm },
    { Format::R11G11B10_FLOAT, MTL::PixelFormatRG11B10Float },
    { Format::RG16_UINT, MTL::PixelFormatRG16Uint },
    { Format::RG16_SINT, MTL::PixelFormatRG16Sint },
    { Format::RG16_UNORM, MTL::PixelFormatRG16Unorm },
    { Format::RG16_SNORM, MTL::PixelFormatRG16Snorm },
    { Format::RG16_FLOAT, MTL::PixelFormatRG16Float },
    { Format::R32_UINT, MTL::PixelFormatR32Uint },
    { Format::R32_SINT, MTL::PixelFormatR32Sint },
    { Format::R32_FLOAT, MTL::PixelFormatR32Float },
    { Format::RGBA16_UINT, MTL::PixelFormatRGBA16Uint },
    { Format::RGBA16_SINT, MTL::PixelFormatRGBA16Sint },
    { Format::RGBA16_FLOAT, MTL::PixelFormatRGBA16Float },
    { Format::RGBA16_UNORM, MTL::PixelFormatRGBA16Unorm },
    { Format::RGBA16_SNORM, MTL::PixelFormatRGBA16Snorm },
    { Format::RG32_UINT, MTL::PixelFormatRG32Uint },
    { Format::RG32_SINT, MTL::PixelFormatRG32Sint },
    { Format::RG32_FLOAT, MTL::PixelFormatRG32Float },
    { Format::RGB32_UINT, MTL::PixelFormatInvalid },
    { Format::RGB32_SINT, MTL::PixelFormatInvalid },
    { Format::RGB32_FLOAT, MTL::PixelFormatInvalid },
    { Format::RGBA32_UINT, MTL::PixelFormatRGBA32Uint },
    { Format::RGBA32_SINT, MTL::PixelFormatRGBA32Sint },
    { Format::RGBA32_FLOAT, MTL::PixelFormatRGBA32Float },
    { Format::D16, MTL::PixelFormatDepth16Unorm },
    { Format::D24S8, MTL::PixelFormatDepth24Unorm_Stencil8 },
    { Format::X24G8_UINT, MTL::PixelFormatDepth24Unorm_Stencil8 },
    { Format::D32, MTL::PixelFormatDepth32Float },
    { Format::D32S8, MTL::PixelFormatDepth32Float_Stencil8 },
    { Format::X32G8_UINT, MTL::PixelFormatX32_Stencil8 },
    { Format::BC1_UNORM, MTL::PixelFormatBC1_RGBA },
    { Format::BC1_UNORM_SRGB, MTL::PixelFormatBC1_RGBA_sRGB },
    { Format::BC2_UNORM, MTL::PixelFormatBC2_RGBA },
    { Format::BC2_UNORM_SRGB, MTL::PixelFormatBC2_RGBA_sRGB },
    { Format::BC3_UNORM, MTL::PixelFormatBC3_RGBA },
    { Format::BC3_UNORM_SRGB, MTL::PixelFormatBC3_RGBA_sRGB },
    { Format::BC4_UNORM, MTL::PixelFormatBC4_RUnorm },
    { Format::BC4_SNORM, MTL::PixelFormatBC4_RSnorm },
    { Format::BC5_UNORM, MTL::PixelFormatBC5_RGUnorm },
    { Format::BC5_SNORM, MTL::PixelFormatBC5_RGSnorm },
    { Format::BC6H_UFLOAT, MTL::PixelFormatBC6H_RGBUfloat },
    { Format::BC6H_SFLOAT, MTL::PixelFormatBC6H_RGBFloat },
    { Format::BC7_UNORM, MTL::PixelFormatBC7_RGBAUnorm },
    { Format::BC7_UNORM_SRGB, MTL::PixelFormatBC7_RGBAUnorm_sRGB },
} };

MTL::PixelFormat Device::convertFormat(Format format)
{
    assert(format < Format::COUNT);
    assert(c_FormatMap[static_cast<uint32_t>(format)].rhiFormat == format);
    return c_FormatMap[static_cast<uint32_t>(format)].mtlFormat;
}

Format Device::reverseFormat(const MTL::PixelFormat mtlFormat)
{
    for(const FormatMapping& mapping : c_FormatMap)
    {
        if(mapping.mtlFormat == mtlFormat)
            return mapping.rhiFormat;
    }
    return Format::UNKNOWN;
}
} // namespace ler::rhi::metal