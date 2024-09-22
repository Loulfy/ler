//
// Created by loulfy on 01/12/2023.
//

#pragma once

#include <array>

namespace ler::rhi
{
    enum class GraphicsAPI : uint8_t
    {
        D3D12,
        VULKAN
    };

    struct Color
    {
        static constexpr std::array<float, 4> Black = {0.f, 0.f, 0.f, 1.f};
        static constexpr std::array<float, 4> White = {1.f, 1.f, 1.f, 1.f};
        static constexpr std::array<float, 4> Red = {1.f, 0.f, 0.f, 1.f};
        static constexpr std::array<float, 4> Green = {0.f, 1.f, 0.f, 1.f};
        static constexpr std::array<float, 4> Blue = {0.f, 0.f, 1.f, 1.f};
        static constexpr std::array<float, 4> Gray = {0.45f, 0.55f, 0.6f, 1.f};
        static constexpr std::array<float, 4> Magenta = {1.f, 0.f, 1.f, 1.f};
        static constexpr std::array<std::array<float, 4>, 10> Palette = {{
            {0.620f, 0.004f, 0.259f, 1.f},
            {0.835f, 0.243f, 0.310f, 1.f},
            {0.957f, 0.427f, 0.263f, 1.f},
            {0.992f, 0.682f, 0.380f, 1.f},
            {0.996f, 0.878f, 0.545f, 1.f},
            {0.902f, 0.961f, 0.596f, 1.f},
            {0.671f, 0.867f, 0.643f, 1.f},
            {0.400f, 0.761f, 0.647f, 1.f},
            {0.196f, 0.533f, 0.741f, 1.f},
            {0.369f, 0.310f, 0.635f, 1.f}
        }};
    };

    enum ResourceState
    {
        Undefined = 0,
        ConstantBuffer = 0x1,
        IndexBuffer = 0x2,
        RenderTarget = 0x4,
        UnorderedAccess = 0x8,
        DepthWrite = 0x10,
        DepthRead = 0x20,
        PixelShader = 0x80,
        ShaderResource = 0x40 | 0x80,
        Indirect = 0x200,
        CopyDest = 0x400,
        CopySrc = 0x800,
        Present = 0x1000,
        Common = 0x2000,
        Raytracing = 0x4000,
        ShadingRateSrc = 0x8000,
    };

    enum class QueueType : uint8_t
    {
        Graphics,
        Compute,
        Transfer,
        Count
    };

    enum class AttachmentLoadOp : uint8_t
    {
        Load = 0,
        Clear,
        DontCare
    };

    enum class SamplerAddressMode : uint8_t
    {
        // D3D names
        Clamp,
        Wrap,
        Border,
        Mirror,
        MirrorOnce,

        // Vulkan names
        ClampToEdge = Clamp,
        Repeat = Wrap,
        ClampToBorder = Border,
        MirroredRepeat = Mirror,
        MirrorClampToEdge = MirrorOnce
    };

    enum class ShaderType : uint16_t
    {
        None            = 0x0000,

        Compute         = 0x0020,

        Vertex          = 0x0001,
        Hull            = 0x0002,
        Domain          = 0x0004,
        Geometry        = 0x0008,
        Pixel           = 0x0010,
        Amplification   = 0x0040,
        Mesh            = 0x0080,
        AllGraphics     = 0x00FE,

        RayGeneration   = 0x0100,
        AnyHit          = 0x0200,
        ClosestHit      = 0x0400,
        Miss            = 0x0800,
        Intersection    = 0x1000,
        Callable        = 0x2000,
        AllRayTracing   = 0x3F00,

        All             = 0x3FFF,
    };

    enum class PrimitiveType : uint8_t
    {
        PointList,
        LineList,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList
    };

    enum class RasterFillMode : uint8_t
    {
        Solid,
        Wireframe,

        // Vulkan names
        Fill = Solid,
        Line = Wireframe
    };

    enum class Format : uint8_t
    {
        UNKNOWN,

        R8_UINT,
        R8_SINT,
        R8_UNORM,
        R8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        R16_UINT,
        R16_SINT,
        R16_UNORM,
        R16_SNORM,
        R16_FLOAT,
        BGRA4_UNORM,
        B5G6R5_UNORM,
        B5G5R5A1_UNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        BGRA8_UNORM,
        SRGBA8_UNORM,
        SBGRA8_UNORM,
        R10G10B10A2_UNORM,
        R11G11B10_FLOAT,
        RG16_UINT,
        RG16_SINT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_FLOAT,
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,

        D16,
        D24S8,
        X24G8_UINT,
        D32,
        D32S8,
        X32G8_UINT,

        BC1_UNORM,
        BC1_UNORM_SRGB,
        BC2_UNORM,
        BC2_UNORM_SRGB,
        BC3_UNORM,
        BC3_UNORM_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_UNORM_SRGB,

        COUNT,
    };
}