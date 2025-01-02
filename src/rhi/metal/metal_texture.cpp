//
// Created by Loulfy on 22/11/2024.
//

#include "rhi/metal.hpp"

namespace ler::rhi::metal
{
TexturePtr Device::createTexture(const TextureDesc& desc)
{
    //auto texture = std::make_shared<Texture>(m_context);
    //populateTexture(texture, desc);
    return nullptr;
}

void Device::populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const
{
    MTL::TextureDescriptor* pTextureDesc = MTL::TextureDescriptor::alloc()->init();

    MTL::TextureUsage usage = MTL::TextureUsageUnknown;
    if(desc.isRenderTarget)
        usage |= MTL::TextureUsageRenderTarget;
    if(desc.isUAV)
        usage |= MTL::TextureUsageShaderWrite;

    pTextureDesc->setWidth(desc.width);
    pTextureDesc->setHeight(desc.height);
    pTextureDesc->setDepth(desc.depth);
    pTextureDesc->setPixelFormat( MTL::PixelFormatRGBA8Unorm );
    pTextureDesc->setTextureType( MTL::TextureType2D );
    pTextureDesc->setStorageMode( MTL::StorageModeManaged );
    pTextureDesc->setUsage(usage);

    texture->handle = m_device->newTexture(pTextureDesc);
    texture->handle->setLabel(NS::String::string(desc.debugName.c_str(), NS::StringEncoding::UTF8StringEncoding));
    pTextureDesc->release();
}
}