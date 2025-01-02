//
// Created by loulfy on 09/03/2024.
//

#pragma once

#include "mesh_list.hpp"

namespace ler::render
{
class IRenderer
{
  public:
    virtual ~IRenderer() = default;
    virtual void render(rhi::CommandPtr& command, const RenderParams& params) = 0;
    virtual void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport){};
    [[nodiscard]] virtual rhi::PipelinePtr getPipeline() const = 0;
    [[nodiscard]] virtual std::string getName() const = 0;
};

class IMeshRenderer
{
  public:
    virtual ~IMeshRenderer() = default;
    virtual void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain, const RenderParams& params) = 0;
    virtual void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const RenderParams& params) = 0;
};
} // namespace ler::render