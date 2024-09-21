//
// Created by loulfy on 13/09/2024.
//

#include "bindless.hpp"

namespace ler::rhi
{
uint32_t CommonBindlessTable::allocate()
{
    return m_textureCount.fetch_add(1, std::memory_order_relaxed);
}

bool CommonBindlessTable::setResource(const ResourcePtr& res, uint32_t slot)
{
    if (m_textureCount > kBindlessMax)
        log::exit("TexturePool Size exceeded");
    m_resources[slot] = res;
    if (std::holds_alternative<TexturePtr>(res))
    {
        const auto& texture = std::get<TexturePtr>(res);
        return visitTexture(texture, slot);
    }
    else if (std::holds_alternative<BufferPtr>(res))
    {
        const auto& buffer = std::get<BufferPtr>(res);
        return visitBuffer(buffer, slot);
    }
    return false;
}

TexturePtr CommonBindlessTable::getTexture(uint32_t slot) const
{
    const ResourcePtr& res = m_resources[slot];
    if (std::holds_alternative<TexturePtr>(res))
        return std::get<TexturePtr>(res);
    return nullptr;
}

BufferPtr CommonBindlessTable::getBuffer(uint32_t slot) const
{
    const ResourcePtr& res = m_resources[slot];
    if (std::holds_alternative<BufferPtr>(res))
        return std::get<BufferPtr>(res);
    return nullptr;
}
} // namespace ler::rhi