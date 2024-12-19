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

    struct CullResources
    {
        uint32_t propIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t drawIndex = 0;
        uint32_t countIndex = 0;
    };

    CullResources test;

    void create(const rhi::DevicePtr& device) override
    {
        rhi::ShaderModule shaderModule("cached/cull.comp", "CSMain", rhi::ShaderType::Compute);
        pipeline = device->createComputePipeline(shaderModule);
        int ii=42;
    }

    void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport) override
    {
    }

    void render(rhi::CommandPtr& cmd, const render::RenderParams& params, render::RenderGraphTable& res) override
    {
        //cmd->pushConstant(pipeline, rhi::ShaderType::Compute, &test, sizeof(test));
        rhi::BufferPtr countBuffer = render::getBufferOutput(res, 1);
        cmd->fillBuffer(countBuffer, 0);
        cmd->dispatch(1 + params.meshList->getInstanceCount() / 32, 1, 1);
    }

    [[nodiscard]] rhi::PipelinePtr getPipeline() const override
    {
        return pipeline;
    }

    [[nodiscard]] std::string getName() const override
    {
        return "CullingCommand";
    }

    /*void getResourceDesc(render::RenderDesc& desc) override
    {
        desc.texture.debugName = desc.name;
        if (desc.name == "draws")
        {
            desc.buffer.isUAV = true;
            desc.buffer.isDrawIndirectArgs = true;
            desc.buffer.byteSize = sizeof(render::DrawCommand) * 128;
        }
        if (desc.name == "count")
        {
            desc.buffer.isUAV = true;
            desc.buffer.isDrawIndirectArgs = true;
            desc.buffer.byteSize = 256;
        }
    }*/

    void createRenderResource(const rhi::DevicePtr& device, const render::RenderParams& params, render::RenderGraphTable& res) override
    {
        rhi::BufferDesc desc;
        desc.isUAV = true;
        desc.isDrawIndirectArgs = true;

        res.outputs.emplace_back();
        res.outputs.back().name = "drawBuffer";
        res.outputs.back().type = render::RR_StorageBuffer;
        desc.byteSize = sizeof(render::DrawCommand) * params.meshList->getInstanceCount();
        res.outputs.back().resource = device->createBuffer(desc);

        res.outputs.emplace_back();
        res.outputs.back().name = "countBuffer";
        res.outputs.back().type = render::RR_StorageBuffer;
        desc.byteSize = 256;
        res.outputs.back().resource = device->createBuffer(desc);

        res.inputs.emplace_back();
        res.inputs.back().name = "camera";
        res.inputs.back().type = render::RR_ConstantBuffer;

        res.inputs.emplace_back();
        res.inputs.back().name = "instances";
        res.inputs.back().type = render::RR_ReadOnlyBuffer;
    }
};
} // namespace ler::pass