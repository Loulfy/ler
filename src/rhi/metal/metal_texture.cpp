//
// Created by Loulfy on 22/11/2024.
//

#include "rhi/metal.hpp"

namespace ler::rhi::metal
{
TexturePtr Device::createTexture(const TextureDesc& desc)
{
    auto texture = std::make_shared<Texture>(m_context);
    populateTexture(texture, desc);
    return texture;
}

void Device::populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const
{
    MTL::TextureDescriptor* pTextureDesc = MTL::TextureDescriptor::texture2DDescriptor(convertFormat(desc.format), desc.width, desc.height, true);

    MTL::TextureUsage usage = MTL::TextureUsageShaderRead;
    if (desc.isRenderTarget)
        usage |= MTL::TextureUsageRenderTarget;
    if (desc.isUAV)
        usage |= MTL::TextureUsageShaderWrite;

    pTextureDesc->setWidth(desc.width);
    pTextureDesc->setHeight(desc.height);
    //pTextureDesc->setDepth(desc.depth);
    pTextureDesc->setMipmapLevelCount(desc.mipLevels);
    //pTextureDesc->setPixelFormat(convertFormat(desc.format));
    //pTextureDesc->setTextureType(MTL::TextureType2D);
    pTextureDesc->setStorageMode(MTL::StorageModePrivate);
    pTextureDesc->setSampleCount(desc.sampleCount);
    //pTextureDesc->setArrayLength(desc.arrayLayers);
    pTextureDesc->setUsage(usage);

    texture->handle = m_device->newTexture(pTextureDesc);
    texture->view = texture->handle->newTextureView(pTextureDesc->pixelFormat(), MTL::TextureType2DArray, NS::Range(0, desc.mipLevels), NS::Range(0, 1));
    texture->handle->setLabel(NS::String::string(desc.debugName.c_str(), NS::StringEncoding::UTF8StringEncoding));
    //pTextureDesc->release();
}

static constexpr MTL::SamplerAddressMode convertSamplerAddressMode(const SamplerAddressMode addressMode)
{
    switch (addressMode)
    {
    default:
    case SamplerAddressMode::Clamp:
        return MTL::SamplerAddressModeClampToEdge;
    case SamplerAddressMode::Wrap:
        return MTL::SamplerAddressModeRepeat;
    case SamplerAddressMode::Border:
        return MTL::SamplerAddressModeClampToBorderColor;
    case SamplerAddressMode::Mirror:
        return MTL::SamplerAddressModeMirrorRepeat;
    case SamplerAddressMode::MirrorOnce:
        return MTL::SamplerAddressModeMirrorClampToEdge;
    }
}

SamplerPtr Device::createSampler(const SamplerDesc& desc)
{
    auto sampler = std::make_shared<Sampler>();
    sampler->desc = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());

    sampler->desc->setMagFilter(desc.filter ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
    sampler->desc->setMinFilter(desc.filter ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
    sampler->desc->setMipFilter(MTL::SamplerMipFilterNearest);
    sampler->desc->setSAddressMode(convertSamplerAddressMode(desc.addressU));
    sampler->desc->setTAddressMode(convertSamplerAddressMode(desc.addressV));
    sampler->desc->setRAddressMode(convertSamplerAddressMode(desc.addressW));
    sampler->desc->setMaxAnisotropy(1.f);
    sampler->desc->setNormalizedCoordinates(true);
    sampler->desc->setCompareFunction(MTL::CompareFunctionLess);
    sampler->desc->setBorderColor(MTL::SamplerBorderColorOpaqueBlack);
    sampler->desc->setLodMinClamp(0.f);
    sampler->desc->setLodMaxClamp(16.f);
    sampler->desc->setSupportArgumentBuffers(true);

    sampler->handle = NS::TransferPtr(m_device->newSamplerState(sampler->desc.get()));
    return sampler;
}
} // namespace ler::rhi::metal
