//
// Created by loulfy on 13/09/2024.
//

#pragma once

#include "rhi.hpp"

namespace ler::rhi
{
class CommonBindlessTable : public IBindlessTable
{
  public:
    explicit CommonBindlessTable(uint32_t count);
    void freeBindlessIndex(uint32_t slot) override;
    [[nodiscard]] uint32_t newBindlessIndex() override;
    [[nodiscard]] ResourceViewPtr createResourceView(const ResourcePtr& res) override;
    [[nodiscard]] TexturePtr getTexture(uint32_t slot) const override;
    [[nodiscard]] BufferPtr getBuffer(uint32_t slot) const override;
    [[nodiscard]] std::lock_guard<std::mutex> lock() override;

    [[nodiscard]] uint32_t getResourceCount() const { return m_size; }

    static constexpr uint32_t kBindlessMax = 1024;

    virtual bool visitTexture(const TexturePtr& texture, uint32_t slot) = 0;
    virtual bool visitBuffer(const BufferPtr& buffer, uint32_t slot) = 0;

    void freeDeferredSlot(uint32_t frameIndex);

  private:
    bool setResource(const ResourcePtr& res, uint32_t slot);

    std::array<ResourcePtr, kBindlessMax> m_resources;
    std::array<SamplerPtr, 16> m_samplers;
    std::mutex m_mutex;
    uint32_t m_size = 0u;
    std::unique_ptr<uint32_t[]> m_freeList;
    std::array<std::vector<uint32_t>, ISwapChain::FrameCount> m_deferredFreeIndices;
};
} // namespace ler::rhi
