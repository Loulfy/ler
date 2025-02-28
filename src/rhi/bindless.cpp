//
// Created by loulfy on 13/09/2024.
//

#include "bindless.hpp"

namespace ler::rhi
{
uint32_t ShaderResourceView::getBindlessIndex() const
{
    return m_index;
}

void ShaderResourceView::destroy()
{
    if (m_allocator)
        m_allocator->freeBindlessIndex(m_index);
    m_allocator = nullptr;
    m_index = UINT32_MAX;
}

ShaderResourceView::~ShaderResourceView()
{
    destroy();
}

CommonBindlessTable::CommonBindlessTable(uint32_t count)
{
    m_freeList = std::make_unique<uint32_t[]>(count);
    for (const uint32_t index : std::views::iota(0u, count))
        m_freeList[index] = index;
}

uint32_t CommonBindlessTable::newBindlessIndex()
{
    //std::lock_guard lock(m_mutex);
    const uint32_t index = m_freeList[m_size];
    ++m_size;
    return index;

    // return m_textureCount.fetch_add(1, std::memory_order_relaxed);
}

void CommonBindlessTable::freeBindlessIndex(uint32_t index)
{
    //std::lock_guard lock(m_mutex);
    const uint32_t frameIndex{};
    m_deferredFreeIndices[frameIndex].push_back(index);
}

void CommonBindlessTable::freeDeferredSlot(uint32_t frameIndex)
{
    //std::lock_guard lock(m_mutex);
    for (uint32_t index : m_deferredFreeIndices[frameIndex])
    {
        --m_size;
        m_freeList[m_size] = index;
    }
    m_deferredFreeIndices[frameIndex].clear();
}

bool CommonBindlessTable::setResource(const ResourcePtr& res, uint32_t slot)
{
    if (slot > kBindlessMax)
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

ResourceViewPtr CommonBindlessTable::createResourceView(const ResourcePtr& res)
{
    ResourceViewPtr view = std::make_shared<ShaderResourceView>();
    view->m_index = newBindlessIndex();
    view->m_allocator = this;
    //view->m_resource = res;
    setResource(res, view->m_index);
    return view;
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

std::lock_guard<std::mutex> CommonBindlessTable::lock()
{
    return std::lock_guard(m_mutex);
}
} // namespace ler::rhi