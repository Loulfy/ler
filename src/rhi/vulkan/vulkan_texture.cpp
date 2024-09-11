//
// Created by loulfy on 02/12/2023.
//

#include "rhi/vulkan.hpp"
#include "log/log.hpp"

namespace ler::rhi::vulkan
{
    struct FormatMapping
    {
        Format rhiFormat;
        VkFormat vkFormat;
    };

    static const std::array<FormatMapping, size_t(Format::COUNT)> c_FormatMap = {{
        { Format::UNKNOWN,           VK_FORMAT_UNDEFINED                },
        { Format::R8_UINT,           VK_FORMAT_R8_UINT                  },
        { Format::R8_SINT,           VK_FORMAT_R8_SINT                  },
        { Format::R8_UNORM,          VK_FORMAT_R8_UNORM                 },
        { Format::R8_SNORM,          VK_FORMAT_R8_SNORM                 },
        { Format::RG8_UINT,          VK_FORMAT_R8G8_UINT                },
        { Format::RG8_SINT,          VK_FORMAT_R8G8_SINT                },
        { Format::RG8_UNORM,         VK_FORMAT_R8G8_UNORM               },
        { Format::RG8_SNORM,         VK_FORMAT_R8G8_SNORM               },
        { Format::R16_UINT,          VK_FORMAT_R16_UINT                 },
        { Format::R16_SINT,          VK_FORMAT_R16_SINT                 },
        { Format::R16_UNORM,         VK_FORMAT_R16_UNORM                },
        { Format::R16_SNORM,         VK_FORMAT_R16_SNORM                },
        { Format::R16_FLOAT,         VK_FORMAT_R16_SFLOAT               },
        { Format::BGRA4_UNORM,       VK_FORMAT_B4G4R4A4_UNORM_PACK16    },
        { Format::B5G6R5_UNORM,      VK_FORMAT_B5G6R5_UNORM_PACK16      },
        { Format::B5G5R5A1_UNORM,    VK_FORMAT_B5G5R5A1_UNORM_PACK16    },
        { Format::RGBA8_UINT,        VK_FORMAT_R8G8B8A8_UINT            },
        { Format::RGBA8_SINT,        VK_FORMAT_R8G8B8A8_SINT            },
        { Format::RGBA8_UNORM,       VK_FORMAT_R8G8B8A8_UNORM           },
        { Format::RGBA8_SNORM,       VK_FORMAT_R8G8B8A8_SNORM           },
        { Format::BGRA8_UNORM,       VK_FORMAT_B8G8R8A8_UNORM           },
        { Format::SRGBA8_UNORM,      VK_FORMAT_R8G8B8A8_SRGB            },
        { Format::SBGRA8_UNORM,      VK_FORMAT_B8G8R8A8_SRGB            },
        { Format::R10G10B10A2_UNORM, VK_FORMAT_A2B10G10R10_UNORM_PACK32 },
        { Format::R11G11B10_FLOAT,   VK_FORMAT_B10G11R11_UFLOAT_PACK32  },
        { Format::RG16_UINT,         VK_FORMAT_R16G16_UINT              },
        { Format::RG16_SINT,         VK_FORMAT_R16G16_SINT              },
        { Format::RG16_UNORM,        VK_FORMAT_R16G16_UNORM             },
        { Format::RG16_SNORM,        VK_FORMAT_R16G16_SNORM             },
        { Format::RG16_FLOAT,        VK_FORMAT_R16G16_SFLOAT            },
        { Format::R32_UINT,          VK_FORMAT_R32_UINT                 },
        { Format::R32_SINT,          VK_FORMAT_R32_SINT                 },
        { Format::R32_FLOAT,         VK_FORMAT_R32_SFLOAT               },
        { Format::RGBA16_UINT,       VK_FORMAT_R16G16B16A16_UINT        },
        { Format::RGBA16_SINT,       VK_FORMAT_R16G16B16A16_SINT        },
        { Format::RGBA16_FLOAT,      VK_FORMAT_R16G16B16A16_SFLOAT      },
        { Format::RGBA16_UNORM,      VK_FORMAT_R16G16B16A16_UNORM       },
        { Format::RGBA16_SNORM,      VK_FORMAT_R16G16B16A16_SNORM       },
        { Format::RG32_UINT,         VK_FORMAT_R32G32_UINT              },
        { Format::RG32_SINT,         VK_FORMAT_R32G32_SINT              },
        { Format::RG32_FLOAT,        VK_FORMAT_R32G32_SFLOAT            },
        { Format::RGB32_UINT,        VK_FORMAT_R32G32B32_UINT           },
        { Format::RGB32_SINT,        VK_FORMAT_R32G32B32_SINT           },
        { Format::RGB32_FLOAT,       VK_FORMAT_R32G32B32_SFLOAT         },
        { Format::RGBA32_UINT,       VK_FORMAT_R32G32B32A32_UINT        },
        { Format::RGBA32_SINT,       VK_FORMAT_R32G32B32A32_SINT        },
        { Format::RGBA32_FLOAT,      VK_FORMAT_R32G32B32A32_SFLOAT      },
        { Format::D16,               VK_FORMAT_D16_UNORM                },
        { Format::D24S8,             VK_FORMAT_D24_UNORM_S8_UINT        },
        { Format::X24G8_UINT,        VK_FORMAT_D24_UNORM_S8_UINT        },
        { Format::D32,               VK_FORMAT_D32_SFLOAT               },
        { Format::D32S8,             VK_FORMAT_D32_SFLOAT_S8_UINT       },
        { Format::X32G8_UINT,        VK_FORMAT_D32_SFLOAT_S8_UINT       },
        { Format::BC1_UNORM,         VK_FORMAT_BC1_RGBA_UNORM_BLOCK     },
        { Format::BC1_UNORM_SRGB,    VK_FORMAT_BC1_RGBA_SRGB_BLOCK      },
        { Format::BC2_UNORM,         VK_FORMAT_BC2_UNORM_BLOCK          },
        { Format::BC2_UNORM_SRGB,    VK_FORMAT_BC2_SRGB_BLOCK           },
        { Format::BC3_UNORM,         VK_FORMAT_BC3_UNORM_BLOCK          },
        { Format::BC3_UNORM_SRGB,    VK_FORMAT_BC3_SRGB_BLOCK           },
        { Format::BC4_UNORM,         VK_FORMAT_BC4_UNORM_BLOCK          },
        { Format::BC4_SNORM,         VK_FORMAT_BC4_SNORM_BLOCK          },
        { Format::BC5_UNORM,         VK_FORMAT_BC5_UNORM_BLOCK          },
        { Format::BC5_SNORM,         VK_FORMAT_BC5_SNORM_BLOCK          },
        { Format::BC6H_UFLOAT,       VK_FORMAT_BC6H_UFLOAT_BLOCK        },
        { Format::BC6H_SFLOAT,       VK_FORMAT_BC6H_SFLOAT_BLOCK        },
        { Format::BC7_UNORM,         VK_FORMAT_BC7_UNORM_BLOCK          },
        { Format::BC7_UNORM_SRGB,    VK_FORMAT_BC7_SRGB_BLOCK           },
    } };

    vk::Format Device::convertFormat(Format format)
    {
        assert(format < Format::COUNT);
        assert(c_FormatMap[uint32_t(format)].rhiFormat == format);
        return static_cast<vk::Format>(c_FormatMap[uint32_t(format)].vkFormat);
    }

    Format Device::reverseFormat(vk::Format format)
    {
        const auto vkFormat = static_cast<VkFormat>(format);
        for(const FormatMapping& mapping : c_FormatMap)
        {
            if(mapping.vkFormat == vkFormat)
                return mapping.rhiFormat;
        }
        return Format::UNKNOWN;
    }

    vk::SamplerAddressMode convertSamplerAddressMode(SamplerAddressMode mode)
    {
        switch(mode)
        {
            case SamplerAddressMode::ClampToEdge:
                return vk::SamplerAddressMode::eClampToEdge;

            case SamplerAddressMode::Repeat:
                return vk::SamplerAddressMode::eRepeat;

            case SamplerAddressMode::ClampToBorder:
                return vk::SamplerAddressMode::eClampToBorder;

            case SamplerAddressMode::MirroredRepeat:
                return vk::SamplerAddressMode::eMirroredRepeat;

            case SamplerAddressMode::MirrorClampToEdge:
                return vk::SamplerAddressMode::eMirrorClampToEdge;

            default:
                return vk::SamplerAddressMode(0);
        }
    }

    vk::SampleCountFlagBits Device::pickImageSample(uint32_t samples)
    {
        switch(samples)
        {
            default:
            case 1:
                return vk::SampleCountFlagBits::e1;
            case 2:
                return vk::SampleCountFlagBits::e2;
            case 4:
                return vk::SampleCountFlagBits::e4;
            case 8:
                return vk::SampleCountFlagBits::e8;
        }
    }

    vk::ImageType pickImageType(uint32_t dimension)
    {
        switch(dimension)
        {
            default:
            case 1:
                return vk::ImageType::e1D;
            case 2:
                return vk::ImageType::e2D;
            case 3:
                return vk::ImageType::e3D;
        }
    }

    vk::ImageAspectFlags Device::guessImageAspectFlags(vk::Format format, bool stencil)
    {
        switch (format)
        {
            case vk::Format::eD16Unorm:
            case vk::Format::eX8D24UnormPack32:
            case vk::Format::eD32Sfloat:
                return vk::ImageAspectFlagBits::eDepth;

            case vk::Format::eS8Uint:
                return vk::ImageAspectFlagBits::eStencil;

            case vk::Format::eD16UnormS8Uint:
            case vk::Format::eD24UnormS8Uint:
            case vk::Format::eD32SfloatS8Uint:
            {
                if(stencil)
                    return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
                else
                    return vk::ImageAspectFlagBits::eDepth;
            }

            default:
                return vk::ImageAspectFlagBits::eColor;
        }
    }

    static bool hasDepth(vk::Format format)
    {
        return Device::guessImageAspectFlags(format, false) == vk::ImageAspectFlagBits::eDepth;
    }

    static vk::ImageUsageFlags pickImageUsage(const TextureDesc& desc, vk::Format format)
    {
        vk::ImageUsageFlags ret = vk::ImageUsageFlagBits::eTransferSrc |
                                  vk::ImageUsageFlagBits::eTransferDst |
                                  vk::ImageUsageFlagBits::eSampled;

        if (desc.isRenderTarget)
        {
            ret |= vk::ImageUsageFlagBits::eInputAttachment;
            if(hasDepth(format))
                ret |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
            else
                ret |= vk::ImageUsageFlagBits::eColorAttachment;
        }

        if(desc.isUAV)
            ret |= vk::ImageUsageFlagBits::eStorage;

        return ret;
    }

    void Device::populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const
    {
        texture->allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        texture->allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        vk::Format format = convertFormat(desc.format);
        texture->info = vk::ImageCreateInfo();
        texture->info.setImageType(pickImageType(desc.dimension));
        texture->info.setExtent(vk::Extent3D(desc.width, desc.height, desc.depth));
        texture->info.setMipLevels(desc.mipLevels);
        texture->info.setArrayLayers(desc.arrayLayers);
        texture->info.setFormat(format);
        texture->info.setInitialLayout(vk::ImageLayout::eUndefined);
        texture->info.setUsage(pickImageUsage(desc, format));
        texture->info.setSharingMode(vk::SharingMode::eExclusive);
        texture->info.setSamples(pickImageSample(desc.sampleCount));
        texture->info.setFlags({});
        texture->info.setTiling(vk::ImageTiling::eOptimal);

        VkResult res = vmaCreateImage(m_context.allocator, reinterpret_cast<VkImageCreateInfo*>(&texture->info), &texture->allocInfo,
                       reinterpret_cast<VkImage*>(&texture->handle), &texture->allocation, nullptr);
        assert(res == VK_SUCCESS);
        texture->setName(desc.debugName);
    }

    void Texture::setName(const std::string& debugName)
    {
        name = debugName;
        if(!m_context.debug)
            return;
        vk::DebugUtilsObjectNameInfoEXT nameInfo;
        nameInfo.setObjectType(vk::ObjectType::eImage);
        VkImage raw = static_cast<VkImage>(handle);
        nameInfo.setObjectHandle((uint64_t)raw);
        nameInfo.setPObjectName(debugName.c_str());
        m_context.device.setDebugUtilsObjectNameEXT(nameInfo);
    }

    vk::ImageView Texture::view(vk::ImageSubresourceRange subresource)
    {
        if(m_views.contains(subresource))
            return m_views.at(subresource).get();

        subresource.setAspectMask(Device::guessImageAspectFlags(info.format, false));

        vk::ImageViewCreateInfo createInfo;
        createInfo.setImage(handle);
        createInfo.setViewType(subresource.layerCount == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray);
        createInfo.setFormat(info.format);
        createInfo.setSubresourceRange(subresource);

        auto iter_pair = m_views.emplace(subresource, m_context.device.createImageViewUnique(createInfo));
        return std::get<0>(iter_pair)->second.get();
    }

    SamplerPtr Device::createSampler(const SamplerDesc& desc)
    {
        auto sampler = std::make_shared<Sampler>();

        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.setMagFilter(desc.filter ? vk::Filter::eLinear : vk::Filter::eNearest);
        samplerInfo.setMinFilter(desc.filter ? vk::Filter::eLinear : vk::Filter::eNearest);
        samplerInfo.setMipmapMode(vk::SamplerMipmapMode::eNearest);
        samplerInfo.setAddressModeU(convertSamplerAddressMode(desc.addressU));
        samplerInfo.setAddressModeV(convertSamplerAddressMode(desc.addressV));
        samplerInfo.setAddressModeW(convertSamplerAddressMode(desc.addressW));
        samplerInfo.setMipLodBias(0.f);
        samplerInfo.setAnisotropyEnable(false);
        samplerInfo.setMaxAnisotropy(1.f);
        samplerInfo.setCompareEnable(false);
        samplerInfo.setCompareOp(vk::CompareOp::eLess);
        samplerInfo.setMinLod(0.f);
        samplerInfo.setMaxLod(std::numeric_limits<float>::max());
        samplerInfo.setBorderColor(vk::BorderColor::eFloatOpaqueBlack);

        sampler->handle = m_context.device.createSamplerUnique(samplerInfo, nullptr);

        return sampler;
    }
}