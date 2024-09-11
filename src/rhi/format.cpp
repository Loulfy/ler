//
// Created by loulfy on 07/01/2024.
//

#include "rhi.hpp"
#include "storage.hpp"

namespace ler::rhi
{
FormatBlockInfo formatToBlockInfo(Format format)
{
    FormatBlockInfo pFormatSize;
    switch (format)
    {
    default:
    case Format::RGBA8_UNORM:
        pFormatSize.blockSizeByte = 4;
        pFormatSize.blockWidth = 1;
        pFormatSize.blockHeight = 1;
        break;
    case Format::BC1_UNORM:
    case Format::BC1_UNORM_SRGB:
    case Format::BC4_UNORM:
    case Format::BC4_SNORM:
        pFormatSize.blockSizeByte = 8;
        pFormatSize.blockWidth = 4;
        pFormatSize.blockHeight = 4;
        break;
    case Format::BC2_UNORM:
    case Format::BC2_UNORM_SRGB:
    case Format::BC3_UNORM:
    case Format::BC3_UNORM_SRGB:
    case Format::BC5_UNORM:
    case Format::BC5_SNORM:
    case Format::BC6H_UFLOAT:
    case Format::BC6H_SFLOAT:
    case Format::BC7_UNORM:
    case Format::BC7_UNORM_SRGB:
        pFormatSize.blockSizeByte = 16;
        pFormatSize.blockWidth = 4;
        pFormatSize.blockHeight = 4;
        break;
    }
    return pFormatSize;
}

std::string to_string(ShaderType stageType)
{
    switch (stageType)
    {
    default:
    case ShaderType::None:
        return "";
    case ShaderType::Compute:
        return "Compute";
    case ShaderType::Vertex:
        return "Vertex";
    case ShaderType::Hull:
        return "Hull";
    case ShaderType::Domain:
        return "Domain";
    case ShaderType::Geometry:
        return "Geometry";
    case ShaderType::Pixel:
        return "Fragment";
    case ShaderType::Amplification:
        return "Amplification";
    case ShaderType::Mesh:
        return "Mesh";
    case ShaderType::RayGeneration:
        return "RayGeneration";
    case ShaderType::AnyHit:
        return "AnyHit";
    case ShaderType::ClosestHit:
        return "ClosestHit";
    case ShaderType::Miss:
        return "Miss";
    case ShaderType::Intersection:
        return "Intersection";
    case ShaderType::Callable:
        return "Callable";
    }
}

std::string to_string(ResourceState state)
{
    switch (state)
    {
    default:
    case Undefined:
        return "Undefined";
    case ConstantBuffer:
        return "ConstantBuffer";
    case IndexBuffer:
        return "IndexBuffer";
    case RenderTarget:
        return "RenderTarget";
    case UnorderedAccess:
        return "UnorderedAccess";
    case DepthWrite:
        return "DepthWrite";
    case DepthRead:
        return "DepthRead";
    case PixelShader:
        return "PixelShader";
    case ShaderResource:
        return "ShaderResource";
    case Indirect:
        return "Indirect";
    case CopyDest:
        return "CopyDest";
    case CopySrc:
        return "CopySrc";
    case Present:
        return "Present";
    case Common:
        return "Common";
    case Raytracing:
        return "Raytracing";
    case ShadingRateSrc:
        return "ShadingRateSrc";
    }
}

static const StringFormatMapping c_StringMappings[] = {
    { Format::UNKNOWN, "UNKNOWN" },

    { Format::R8_UINT, "R8_UINT" },
    { Format::R8_SINT, "R8_SINT" },
    { Format::R8_UNORM, "R8_UNORM" },
    { Format::R8_SNORM, "R8_SNORM " },
    { Format::RG8_UINT, "RG8_UINT" },
    { Format::RG8_SINT, "RG8_SINT" },
    { Format::RG8_UNORM, "RG8_UNORM" },
    { Format::RG8_SNORM, "RG8_SNORM" },
    { Format::R16_UINT, "R16_UINT" },
    { Format::R16_SINT, "R16_SINT" },
    { Format::R16_UNORM, "R16_UNORM" },
    { Format::R16_SNORM, "R16_SNORM" },
    { Format::R16_FLOAT, "R16_FLOAT" },
    { Format::BGRA4_UNORM, "BGRA4_UNORM" },
    { Format::B5G6R5_UNORM, "B5G6R5_UNORM" },
    { Format::B5G5R5A1_UNORM, "B5G5R5A1_UNORM" },
    { Format::RGBA8_UINT, "RGBA8_UINT" },
    { Format::RGBA8_SINT, "RGBA8_SINT" },
    { Format::RGBA8_UNORM, "RGBA8_UNORM" },
    { Format::RGBA8_SNORM, "RGBA8_SNORM" },
    { Format::BGRA8_UNORM, "BGRA8_UNORM" },
    { Format::SRGBA8_UNORM, "SRGBA8_UNORM" },
    { Format::SBGRA8_UNORM, "SBGRA8_UNORM" },
    { Format::R10G10B10A2_UNORM, "R10G10B10A2_UNORM" },
    { Format::R11G11B10_FLOAT, "R11G11B10_FLOAT" },
    { Format::RG16_UINT, "RG16_UINT" },
    { Format::RG16_SINT, "RG16_SINT" },
    { Format::RG16_UNORM, "RG16_UNORM" },
    { Format::RG16_SNORM, "RG16_SNORM" },
    { Format::RG16_FLOAT, "RG16_FLOAT" },
    { Format::R32_UINT, "R32_UINT" },
    { Format::R32_SINT, "R32_SINT" },
    { Format::R32_FLOAT, "R32_FLOAT" },
    { Format::RGBA16_UINT, "RGBA16_UINT" },
    { Format::RGBA16_SINT, "RGBA16_SINT" },
    { Format::RGBA16_FLOAT, "RGBA16_FLOAT" },
    { Format::RGBA16_UNORM, "RGBA16_UNORM" },
    { Format::RGBA16_SNORM, "RGBA16_SNORM" },
    { Format::RG32_UINT, "RG32_UINT" },
    { Format::RG32_SINT, "RG32_SINT" },
    { Format::RG32_FLOAT, "RG32_FLOAT" },
    { Format::RGB32_UINT, "RGB32_UINT" },
    { Format::RGB32_SINT, "RGB32_SINT" },
    { Format::RGB32_FLOAT, "RGB32_FLOAT" },
    { Format::RGBA32_UINT, "RGBA32_UINT" },
    { Format::RGBA32_SINT, "RGBA32_SINT" },
    { Format::RGBA32_FLOAT, "RGBA32_FLOAT" },

    { Format::D16, "D16" },
    { Format::D24S8, "D24S8" },
    { Format::X24G8_UINT, "X24G8_UINT" },
    { Format::D32, "D32" },
    { Format::D32S8, "D32S8" },
    { Format::X32G8_UINT, "X32G8_UINT" },

    { Format::BC1_UNORM, "BC1_UNORM" },
    { Format::BC1_UNORM_SRGB, "BC1_UNORM_SRGB" },
    { Format::BC2_UNORM, "BC2_UNORM" },
    { Format::BC2_UNORM_SRGB, "BC2_UNORM_SRGB" },
    { Format::BC3_UNORM, "BC3_UNORM" },
    { Format::BC3_UNORM_SRGB, "BC3_UNORM_SRGB" },
    { Format::BC4_UNORM, "BC4_UNORM" },
    { Format::BC4_SNORM, "BC4_SNORM" },
    { Format::BC5_UNORM, "BC5_UNORM" },
    { Format::BC5_SNORM, "BC5_SNORM" },
    { Format::BC6H_UFLOAT, "BC6H_UFLOAT" },
    { Format::BC6H_SFLOAT, "BC6H_SFLOAT" },
    { Format::BC7_UNORM, "BC7_UNORM" },
    { Format::BC7_UNORM_SRGB, "BC7_UNORM_SRGB" },
};

std::string to_string(Format format)
{
    const StringFormatMapping& mapping = c_StringMappings[uint32_t(format)];
    assert(mapping.format == format);
    return mapping.name;
}

Format stringToFormat(const std::string& name)
{
    for (const auto& mapping : c_StringMappings)
    {
        if (mapping.name == name)
            return mapping.format;
    }

    return Format::UNKNOWN;
}
} // namespace ler::rhi