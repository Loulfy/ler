//
// Created by Loulfy on 09/03/2025.
//

#include "importer.hpp"
#include "sys/utils.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <cmp_core.h>
#include <compressonator.h>
#include <fstream>
#include <stb_image.h>
#include <xxhash.h>

namespace ler::pak
{
constexpr uint64_t D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256;
constexpr uint64_t D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT = 512;
constexpr uint64_t D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES = 65536;

void CMP_LoadTexture(const aiTexture* texture, MipSet* mipSet)
{
    const auto* buffer = reinterpret_cast<const unsigned char*>(texture->pcData);

    int width, height, componentCount;
    stbi_uc* image = stbi_load_from_memory(buffer, static_cast<int>(texture->mWidth), &width, &height, &componentCount,
                                           STBI_rgb_alpha);

    CMP_CMIPS CMips;

    if (!CMips.AllocateMipSet(mipSet, CF_8bit, TDT_ARGB, TT_2D, width, height, 1))
        return;

    if (!CMips.AllocateMipLevelData(CMips.GetMipLevel(mipSet, 0), width, height, CF_8bit, TDT_ARGB))
        return;

    mipSet->m_nMipLevels = 1;
    mipSet->m_format = CMP_FORMAT_RGBA_8888;

    CMP_BYTE* pData = CMips.GetMipLevel(mipSet, 0)->m_pbData;

    // RGBA : 8888 = 4 bytes
    CMP_DWORD dwPitch = (4 * mipSet->m_nWidth);
    CMP_DWORD dwSize = dwPitch * mipSet->m_nHeight;

    memcpy(pData, image, dwSize);

    mipSet->pData = pData;
    mipSet->dwDataSize = dwSize;

    stbi_image_free(image);
}

static constexpr std::string_view CMPErrorToString(CMP_ERROR error)
{
    switch (error)
    {
    case CMP_OK:
        return "Ok.";
    case CMP_ABORTED:
        return "The conversion was aborted.";
    case CMP_ERR_INVALID_SOURCE_TEXTURE:
        return "The source texture is invalid.";
    case CMP_ERR_INVALID_DEST_TEXTURE:
        return "The destination texture is invalid.";
    case CMP_ERR_UNSUPPORTED_SOURCE_FORMAT:
        return "The source format is not a supported format.";
    case CMP_ERR_UNSUPPORTED_DEST_FORMAT:
        return "The destination format is not a supported format.";
    case CMP_ERR_UNSUPPORTED_GPU_ASTC_DECODE:
        return "The gpu hardware is not supported.";
    case CMP_ERR_UNSUPPORTED_GPU_BASIS_DECODE:
        return "The gpu hardware is not supported.";
    case CMP_ERR_SIZE_MISMATCH:
        return "The source and destination texture sizes do not match.";
    case CMP_ERR_UNABLE_TO_INIT_CODEC:
        return "Compressonator was unable to initialize the codec needed for conversion.";
    case CMP_ERR_UNABLE_TO_INIT_DECOMPRESSLIB:
        return "GPU_Decode Lib was unable to initialize the codec needed for decompression .";
    case CMP_ERR_UNABLE_TO_INIT_COMPUTELIB:
        return "Compute Lib was unable to initialize the codec needed for compression.";
    case CMP_ERR_CMP_DESTINATION:
        return "Error in compressing destination texture";
    case CMP_ERR_MEM_ALLOC_FOR_MIPSET:
        return "Memory Error: allocating MIPSet compression level data buffer";
    case CMP_ERR_UNKNOWN_DESTINATION_FORMAT:
        return "The destination Codec Type is unknown! In SDK refer to GetCodecType()";
    case CMP_ERR_FAILED_HOST_SETUP:
        return "Failed to setup Host for processing";
    case CMP_ERR_PLUGIN_FILE_NOT_FOUND:
        return "The required plugin library was not found";
    case CMP_ERR_UNABLE_TO_LOAD_FILE:
        return "The requested file was not loaded";
    case CMP_ERR_UNABLE_TO_CREATE_ENCODER:
        return "Request to create an encoder failed";
    case CMP_ERR_UNABLE_TO_LOAD_ENCODER:
        return "Unable to load an encode library";
    case CMP_ERR_NOSHADER_CODE_DEFINED:
        return "No shader code is available for the requested framework";
    case CMP_ERR_GPU_DOESNOT_SUPPORT_COMPUTE:
        return "The GPU device selected does not support compute";
    case CMP_ERR_NOPERFSTATS:
        return "No Performance Stats are available";
    case CMP_ERR_GPU_DOESNOT_SUPPORT_CMP_EXT:
        return "The GPU does not support the requested compression extension!";
    case CMP_ERR_GAMMA_OUTOFRANGE:
        return "Gamma value set for processing is out of range";
    case CMP_ERR_PLUGIN_SHAREDIO_NOT_SET:
        return "The plugin C_PluginSetSharedIO call was not set and is required for this plugin to operate";
    case CMP_ERR_UNABLE_TO_INIT_D3DX:
        return "Unable to initialize DirectX SDK or get a specific DX API";
    case CMP_FRAMEWORK_NOT_INITIALIZED:
        return "CMP_InitFramework failed or not called.";
    case CMP_ERR_GENERIC:
        return "An unknown error occurred.";
    default:
        return "Unknown CMP_ERROR";
    }
}

TextureFormat PakPacker::convertCMPFormat(CMP_FORMAT fmt)
{
    switch (fmt)
    {
    default:
        log::error("CMP FORMAT IS MISSING");
        return TextureFormat_Bc1;
    case CMP_FORMAT_BC1:
        return TextureFormat_Bc1;
    case CMP_FORMAT_BC2:
        return TextureFormat_Bc2;
    case CMP_FORMAT_BC3:
        return TextureFormat_Bc3;
    case CMP_FORMAT_BC4:
    case CMP_FORMAT_BC4_S:
        return TextureFormat_Bc4;
    case CMP_FORMAT_BC5:
        return TextureFormat_Bc5;
    case CMP_FORMAT_BC6H:
    case CMP_FORMAT_BC6H_SF:
        return TextureFormat_Bc6;
    case CMP_FORMAT_BC7:
        return TextureFormat_Bc7;
    }
}

static void computePitch(CMP_FORMAT fmt, size_t width, size_t height, size_t& rowPitch, size_t& slicePitch)
{
    uint64_t pitch = 0;
    uint64_t slice = 0;
    switch (fmt)
    {
    case CMP_FORMAT_BC1:
    case CMP_FORMAT_BC4:
    case CMP_FORMAT_BC4_S: {
        const uint64_t nbw = std::max<uint64_t>(1u, (uint64_t(width) + 3u) / 4u);
        const uint64_t nbh = std::max<uint64_t>(1u, (uint64_t(height) + 3u) / 4u);
        pitch = nbw * 8u;
        slice = pitch * nbh;
    }
    break;
    case CMP_FORMAT_BC2:
    case CMP_FORMAT_BC3:
    case CMP_FORMAT_BC5:
    case CMP_FORMAT_BC5_S:
    case CMP_FORMAT_BC6H:
    case CMP_FORMAT_BC6H_SF:
    case CMP_FORMAT_BC7: {
        const uint64_t nbw = std::max<uint64_t>(1u, (uint64_t(width) + 3u) / 4u);
        const uint64_t nbh = std::max<uint64_t>(1u, (uint64_t(height) + 3u) / 4u);
        pitch = nbw * 16u;
        slice = pitch * nbh;
    }
    break;
    default:
        log::error("We mest up!");
        break;
    }

    rowPitch = static_cast<size_t>(pitch);
    slicePitch = static_cast<size_t>(slice);
}

template <typename T> T align(T size, T alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

static void CopyTextureSurface(uint8_t* pSrc, std::byte* pDest, uint64_t rowSizeInBytes, uint32_t numRows,
                               uint32_t numSlices = 1u) // It's the depth, should be always 1 for 2d.
{
    uint64_t rowPitch = align(rowSizeInBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    for (uint32_t z = 0; z < numSlices; ++z)
    {
        for (uint32_t y = 0; y < numRows; ++y)
        {
            memcpy(pDest + y * rowPitch, pSrc + y * rowSizeInBytes, rowSizeInBytes);
        }
        pDest += numRows * rowPitch;
        pSrc += numRows * rowSizeInBytes;
    }
}

static void CMP_SavePaddedRawData(PackedTextureMetadata& metadata, const fs::path& path, CMP_MipSet& mipSet)
{
    size_t rowPitch, slicePitch;
    CMP_MipLevel* mip;

    size_t totalBytes = 0;
    size_t currentOffset = 0;
    std::vector<size_t> offset(mipSet.m_nMipLevels);
    for (int i = 0; i < mipSet.m_nMipLevels; ++i)
    {
        uint32_t w = mipSet.m_nWidth >> i;
        uint32_t h = mipSet.m_nHeight >> i;
        computePitch(mipSet.m_format, w, h, rowPitch, slicePitch);
        size_t height = std::max<size_t>(1, (h + 3) / 4);
        // CMP_GetMipLevel(&mip, &mipSet, i, 0);
        // computePitch(mipSet.m_format, mip->m_nWidth, mip->m_nHeight, rowPitch, slicePitch);
        // size_t height = std::max<size_t>(1, (mip->m_nHeight + 3) / 4);
        size_t subResourceSize = align(rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * height;
        currentOffset = align(currentOffset, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

        offset[i] = currentOffset;
        totalBytes = currentOffset + subResourceSize;
        currentOffset = totalBytes;

        /*totalBytes = currentOffset + subResourceSize;
        offset[i] = currentOffset;
        currentOffset = align(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);*/
    }

    metadata.totalBytes = totalBytes;

    currentOffset = 0;
    CMP_GetMipLevel(&mip, &mipSet, 0, 0);
    std::vector<std::byte> copyableFootprints(totalBytes);
    for (int i = 0; i < mipSet.m_nMipLevels; ++i)
    {
        /*CMP_GetMipLevel(&mip, &mipSet, i, 0);
        computePitch(mipSet.m_format, mip->m_nWidth, mip->m_nHeight, rowPitch, slicePitch);
        size_t height = std::max<size_t>(1, (mip->m_nHeight + 3) / 4);
        log::info("rowPitch: {}", rowPitch);
        CopyTextureSurface(mip->m_pbData, copyableFootprints.data() + offset[i], rowPitch, height);*/

        uint32_t w = mipSet.m_nWidth >> i;
        uint32_t h = mipSet.m_nHeight >> i;
        computePitch(mipSet.m_format, w, h, rowPitch, slicePitch);
        size_t height = std::max<size_t>(1, (h + 3) / 4);
        log::info("rowPitch: {}", rowPitch);
        CopyTextureSurface(mipSet.pData + currentOffset, copyableFootprints.data() + offset[i], rowPitch, height);
        currentOffset += rowPitch * height;
    }

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(copyableFootprints.data()),
               static_cast<int64_t>(copyableFootprints.size()));
    file.close();
}

CMP_BOOL compressionCallback(CMP_FLOAT fProgress, CMP_DWORD_PTR pUser1, CMP_DWORD_PTR pUser2)
{
    UNREFERENCED_PARAMETER(pUser1);
    UNREFERENCED_PARAMETER(pUser2);

    std::printf("\rCompression progress = %3.0f", fProgress);

    return false;
}

void PakPacker::exportTexture(const aiScene* aiScene, const fs::path& path, PackedTextureMetadata& metadata,
                              bool skipCompress)
{
    CMP_InitFramework();

    MipSet mipSetIn = {};

    m_root.make_preferred();
    fs::path pathOut = sys::getHomeDir() / sys::PACKED_DIR / m_root.stem();
    fs::create_directories(pathOut);

    fs::path pathIn = path;
    const aiTexture* em = aiScene->GetEmbeddedTexture(pathIn.string().c_str());
    if (em == nullptr)
    {
        pathIn = m_root / pathIn;
        CMP_LoadTexture(pathIn.string().c_str(), &mipSetIn);
        pathOut /= path.filename();
    }
    else
    {
        CMP_LoadTexture(em, &mipSetIn);
        if (em->mFilename.length == 0)
        {
            std::string stem = path.string();
            uint64_t hash = XXH3_64bits(stem.c_str(), stem.size());
            pathOut /= std::to_string(hash);
        }
        else
        {
            pathOut /= em->mFilename.C_Str();
        }
    }

    pathOut.replace_extension(".gpu");
    pathOut = pathOut.make_preferred();

    if (mipSetIn.m_nMipLevels <= 1)
    {
        CMP_INT requestLevel = 16;
        CMP_INT nMinSize = CMP_CalcMinMipSize(mipSetIn.m_nHeight, mipSetIn.m_nWidth, requestLevel);
        CMP_GenerateMIPLevels(&mipSetIn, nMinSize);
    }

    metadata.width = mipSetIn.m_nWidth;
    metadata.height = mipSetIn.m_nHeight;
    metadata.mipLevels = mipSetIn.m_nMipLevels;
    metadata.gpuFile = pathOut;

    if (pathIn.extension() == ".dds")
        metadata.format = mipSetIn.m_format;

    if (skipCompress)
    {
        CMP_FreeMipSet(&mipSetIn);
        return;
    }

    if (pathIn.extension() == ".dds")
    {
        CMP_SavePaddedRawData(metadata, pathOut, mipSetIn);
        CMP_FreeMipSet(&mipSetIn);
        return;
    }

    // Set Compression Options
    KernelOptions kernel_options = {};

    kernel_options.fquality = 1;
    kernel_options.threads = 0;
    kernel_options.encodeWith = CMP_HPC;
    kernel_options.format = metadata.format;

    //=====================================================
    // Example of using BC1 encoder options
    // kernel_options.bc15 is valid for BC1 to BC5 formats
    //=====================================================
    {
        // Enable punch through alpha setting
        kernel_options.bc15.useAlphaThreshold = true;
        kernel_options.bc15.alphaThreshold = 128;

        // Enable setting channel weights
        kernel_options.bc15.useChannelWeights = true;
        kernel_options.bc15.channelWeights[0] = 0.3086f;
        kernel_options.bc15.channelWeights[1] = 0.6094f;
        kernel_options.bc15.channelWeights[2] = 0.0820f;
    }

    //--------------------------------------------------------------
    // Setup a results buffer for the processed file,
    // the content will be set after the source texture is processed
    // in the call to CMP_ConvertMipTexture()
    //--------------------------------------------------------------
    CMP_MipSet mipSetCmp;
    memset(&mipSetCmp, 0, sizeof(CMP_MipSet));

    // Compress the texture
    CMP_ERROR cmp_status = CMP_ProcessTexture(&mipSetIn, &mipSetCmp, kernel_options, compressionCallback);
    if (cmp_status != CMP_OK)
    {
        CMP_FreeMipSet(&mipSetIn);
        log::error("Compression returned an error: {}", CMPErrorToString(cmp_status));
        return;
    }

    CMP_SavePaddedRawData(metadata, pathOut, mipSetCmp);

    CMP_FreeMipSet(&mipSetIn);
    CMP_FreeMipSet(&mipSetCmp);
}

void PakPacker::processTextures(const aiScene* aiScene, bool cook)
{
    int num = 1;
    for (auto& [filename, metadata] : m_textureMap)
    {
        log::info("[Packer] Processing {}/{}: {}", num, m_textureMap.size(), metadata.filename);
        exportTexture(aiScene, filename, metadata, cook);
        num++;
    }

    PackedTextureMetadata metadata;
    metadata.format = CMP_FORMAT_BC7;
    metadata.width = 4;
    metadata.height = 4;
    metadata.mipLevels = 3;
    metadata.filename = "white";
    metadata.gpuFile = R"(C:\Users\loria\.ler\sponza\white.gpu)";
    m_textureMap.emplace("white.png", metadata);

    concatenateFilesWithAlignment(m_outFile, m_builder, m_entries);
    m_textureMap.clear();
}
} // namespace ler::pak