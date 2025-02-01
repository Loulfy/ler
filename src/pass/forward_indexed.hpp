//
// Created by loulfy on 10/03/2024.
//

#pragma once

#include "render/graph.hpp"

namespace ler::pass
{
class ForwardIndexed : public render::RenderGraphPass
{
  public:
    rhi::PipelinePtr pipeline;
    rhi::BufferPtr ubo;
    rhi::BufferPtr drawsBuffer;
    rhi::BufferPtr countBuffer;

    void create(const rhi::DevicePtr& device) override
    {
        std::vector<rhi::ShaderModule> modules;
        modules.emplace_back("cached/mesh.frag", "PSMain", rhi::ShaderType::Pixel);
        modules.emplace_back("cached/mesh.vert", "VSMain", rhi::ShaderType::Vertex);

        rhi::PipelineDesc pso;
        pso.writeDepth = true;
        pso.fillMode = rhi::RasterFillMode::Fill;
        pso.topology = rhi::PrimitiveType::TriangleList;
        pso.colorAttach.emplace_back(rhi::Format::RGBA8_UNORM);
        pso.depthAttach = rhi::Format::D32;
        pipeline = device->createGraphicsPipeline(modules, pso);

        //graph.getResource(resources[2].handle, ubo);
        //graph.getResource(resources[6].handle, drawsBuffer);
        //graph.getResource(resources[7].handle, countBuffer);
    }
    void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport) override
    {
    }
    void render(rhi::CommandPtr& cmd, const render::RenderParams& params, render::RenderGraphTable& res) override
    {
        render::DrawConstant constant;
        constant.proj = params.proj;
        constant.view = params.view;

        ubo->uploadFromMemory(&constant, sizeof(render::DrawConstant));

        //params.meshes->bind(cmd, false);
        /*for(uint32_t i = 0; i < scene.getInstanceCount(); ++i)
        {
            const auto& draw = scene.getInstance(i);
            const auto& mesh = params.meshes->getMesh(draw.meshIndex);
            cmd->drawIndexedInstanced(mesh.countIndex, mesh.firstIndex, mesh.firstVertex, i);
        }*/
        // cmd->drawIndirectIndexed(pipeline, drawsBuffer, countBuffer, scene.getInstanceCount(),
        // sizeof(render::DrawCommand));

        render::RenderMeshList* meshList = params.meshList;
        meshList->bindVertices(cmd);
        for (uint32_t i = 0; i < meshList->getInstanceCount(); ++i)
        {
            const render::DrawInstance& draw = meshList->getInstance(i);
            const render::IndexedMesh& mesh = meshList->getMesh(draw.meshIndex);
            cmd->drawIndexedPrimitives(mesh.countIndex, mesh.firstIndex, mesh.firstVertex, i);
        }
    }
    [[nodiscard]] rhi::PipelinePtr getPipeline() const override
    {
        return pipeline;
    }

    [[nodiscard]] std::string getName() const override
    {
        return "ForwardIndexed";
    }

    /*void getResourceDesc(render::RenderDesc& desc) override
    {
        desc.texture.debugName = desc.name;
        if (desc.name == "ubo")
        {
            desc.buffer.isStaging = true;
            desc.buffer.isConstantBuffer = true;
            desc.buffer.byteSize = sizeof(render::DrawConstant);
        }
        if (desc.name == "color")
        {
            desc.texture.isRenderTarget = true;
            desc.texture.format = rhi::Format::RGBA8_UNORM;
        }
        if (desc.name == "depth")
        {
            desc.texture.isRenderTarget = true;
            desc.texture.format = rhi::Format::D32;
        }
    }*/
};
} // namespace ler::pass
