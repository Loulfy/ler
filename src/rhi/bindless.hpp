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
    uint32_t allocate() override;
    bool setResource(const ResourcePtr& res, uint32_t slot) override;
    [[nodiscard]] TexturePtr getTexture(uint32_t slot) const override;
    [[nodiscard]] BufferPtr getBuffer(uint32_t slot) const override;

    static constexpr uint32_t kBindlessMax = 1024;

    virtual bool visitTexture(const TexturePtr& texture, uint32_t slot) = 0;
    virtual bool visitBuffer(const BufferPtr& buffer, uint32_t slot) = 0;

  private:
    std::atomic_uint32_t m_textureCount = 0;
    std::array<ResourcePtr, kBindlessMax> m_resources;
    std::array<SamplerPtr, 16> m_samplers;
};
} // namespace ler::rhi
