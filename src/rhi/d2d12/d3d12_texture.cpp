//
// Created by Loulfy on 02/12/2023.
//

#include "log/log.hpp"
#include "rhi/d3d12.hpp"

namespace ler::rhi::d3d12
{
DXGI_FORMAT Device::convertFormatRtv(Format format)
{
    return getDxgiFormatMapping(format).rtvFormat;
}

DXGI_FORMAT Device::convertFormatRes(Format format)
{
    return getDxgiFormatMapping(format).resourceFormat;
}

static constexpr D3D12_TEXTURE_ADDRESS_MODE convertSamplerAddressMode(SamplerAddressMode mode)
{
    switch (mode)
    {
    case SamplerAddressMode::Clamp:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case SamplerAddressMode::Wrap:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case SamplerAddressMode::Border:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case SamplerAddressMode::Mirror:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case SamplerAddressMode::MirrorOnce:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
    default:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }
}

static constexpr bool hasDepth(DXGI_FORMAT format)
{
    switch (format)
    {
    // TODO: complete depth format
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return true;
    default:
        return false;
    }
}

static constexpr D3D12_RESOURCE_FLAGS pickImageUsage(const TextureDesc& desc, DXGI_FORMAT format)
{
    D3D12_RESOURCE_FLAGS ret = D3D12_RESOURCE_FLAG_NONE;

    if (desc.isRenderTarget)
    {
        if (hasDepth(format))
            ret |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        else
            ret |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    if (desc.isUAV)
        ret |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return ret;
}

void Device::populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const
{
    DXGI_FORMAT format = convertFormatRtv(desc.format);
    if (desc.isRenderTarget)
        format = convertFormatRtv(desc.format);
    texture->desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture->desc.Alignment = 0;
    texture->desc.Width = desc.width;
    texture->desc.Height = desc.height;
    texture->desc.DepthOrArraySize = desc.arrayLayers;
    texture->desc.MipLevels = desc.mipLevels;
    texture->desc.Format = format;
    texture->desc.SampleDesc.Count = desc.sampleCount;
    texture->desc.SampleDesc.Quality = 0;
    texture->desc.Layout = desc.isTiled ? D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE : D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture->desc.Flags = pickImageUsage(desc, format);
    texture->state = Common;

    texture->allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    const std::array<float, 4>& color = Color::White;
    D3D12_CLEAR_VALUE clearValue;
    memcpy(clearValue.Color, color.data(), sizeof(float) * 4);
    clearValue.DepthStencil.Depth = 1.f;
    clearValue.Format = texture->desc.Format;
    HRESULT hr = m_allocator->CreateResource(&texture->allocDesc, &texture->desc, D3D12_RESOURCE_STATE_COMMON,
                                             desc.isRenderTarget ? &clearValue : nullptr, &texture->allocation,
                                             IID_NULL, nullptr);

    if (FAILED(hr))
    {
        log::error("Failed to allocate texture");
        return;
    }

    texture->handle = texture->allocation->GetResource();
    std::wstring name = sys::toUtf16(desc.debugName);
    texture->handle->SetName(name.c_str());

    if (desc.isRenderTarget)
    {
        if (hasDepth(texture->desc.Format))
        {
            texture->rtvDescriptor = m_context.descriptorPool[D3D12_DESCRIPTOR_HEAP_TYPE_DSV]->allocate(1);
            m_device->CreateDepthStencilView(texture->handle, nullptr, texture->rtvDescriptor.getCpuHandle());
        }
        else
        {
            texture->rtvDescriptor = m_context.descriptorPool[D3D12_DESCRIPTOR_HEAP_TYPE_RTV]->allocate(1);
            m_device->CreateRenderTargetView(texture->handle, nullptr, texture->rtvDescriptor.getCpuHandle());
        }
    }
}

SamplerPtr Device::createSampler(const SamplerDesc& desc)
{
    auto sampler = std::make_shared<Sampler>();

    sampler->desc.Filter = D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    sampler->desc.AddressU = convertSamplerAddressMode(desc.addressU);
    sampler->desc.AddressV = convertSamplerAddressMode(desc.addressV);
    sampler->desc.AddressW = convertSamplerAddressMode(desc.addressW);
    sampler->desc.MipLODBias = 0.f;
    sampler->desc.MaxAnisotropy = 1.f;
    sampler->desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
    sampler->desc.BorderColor[0] = 0.f;
    sampler->desc.BorderColor[1] = 0.f;
    sampler->desc.BorderColor[2] = 0.f;
    sampler->desc.BorderColor[3] = 1.f;
    sampler->desc.MinLOD = 0.f;
    sampler->desc.MaxLOD = D3D12_FLOAT32_MAX;

    return sampler;
}
} // namespace ler::rhi::d3d12