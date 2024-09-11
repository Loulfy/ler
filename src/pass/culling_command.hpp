//
// Created by loulfy on 18/03/2024.
//

#pragma once

#include "render/graph.hpp"

namespace ler::pass
{
class CullingCommand : public render::RenderGraphPass
{
  public:

    rhi::PipelinePtr pipeline;
    rhi::BufferPtr countBuffer;

    void create(const rhi::DevicePtr& device, render::RenderGraph& graph, std::span<render::RenderDesc> resources) override
    {
        rhi::ShaderModule shaderModule("cached/cull.comp", "CSMain", rhi::ShaderType::Compute);
        pipeline = device->createComputePipeline(shaderModule);

        graph.getResource(resources[3].handle, countBuffer);
    }
    void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport) override
    {

    }
    void render(rhi::CommandPtr& cmd, render::RenderMeshList& scene, render::RenderParams params) override
    {
        cmd->fillBuffer(countBuffer, 0);
        cmd->dispatch(1 + scene.getInstanceCount() / 64, 1, 1);
    }
    [[nodiscard]] rhi::PipelinePtr getPipeline() const override
    {
        return pipeline;
    }

    [[nodiscard]] std::string getName() const override
    {
        return "CullingCommand";
    }

    void getResourceDesc(render::RenderDesc& desc) override
    {
        desc.texture.debugName = desc.name;
        if(desc.name == "draws")
        {
            desc.buffer.isUAV = true;
            desc.buffer.isDrawIndirectArgs = true;
            desc.buffer.byteSize = sizeof(render::DrawCommand) * 128;
        }
        if(desc.name == "count")
        {
            desc.buffer.isUAV = true;
            desc.buffer.isDrawIndirectArgs = true;
            desc.buffer.byteSize = 256;
        }
    }
};
}