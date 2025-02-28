//
// Created by loulfy on 02/11/2024.
//

#pragma once

#include "render/graph.hpp"

namespace ler::pass
{
class DeferredScene final : public rhi::IRenderPass, public render::IMeshRenderer
{
  private:
    rhi::PipelinePtr m_cullPass;
    rhi::PipelinePtr m_wirePass;
    rhi::BindlessTablePtr m_table;
    rhi::TexturePtr m_depth;

    rhi::BufferPtr m_countBuffer;
    rhi::BufferPtr m_drawBuffer;
    rhi::BufferPtr m_frustumBuffer;
    rhi::BufferPtr m_upload;
    rhi::BufferPtr m_readBack;

    rhi::BufferPtr m_test;

    rhi::RenderingInfo pass;
    render::MeshBuffers* meshes = nullptr;

    struct CullResource
    {
        uint32_t propIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t drawIndex = 0;
        uint32_t countIndex = 0;
        uint32_t frustIndex = 0;
    };

    CullResource m_cullRes;

    static void getFrustumPlanes(glm::mat4 mvp, glm::vec4* planes)
    {
        using glm::vec4;

        mvp = glm::transpose(mvp);
        planes[0] = vec4(mvp[3] + mvp[0]); // left
        planes[1] = vec4(mvp[3] - mvp[0]); // right
        planes[2] = vec4(mvp[3] + mvp[1]); // bottom
        planes[3] = vec4(mvp[3] - mvp[1]); // top
        planes[4] = vec4(mvp[3] + mvp[2]); // near
        planes[5] = vec4(mvp[3] - mvp[2]); // far
    }

    static void getFrustumCorners(glm::mat4 mvp, glm::vec4* points)
    {
        using glm::vec4;

        const vec4 corners[] = {
                vec4(-1, -1, -1, 1), vec4(1, -1, -1, 1),
                vec4(1,  1, -1, 1),  vec4(-1,  1, -1, 1),
                vec4(-1, -1,  1, 1), vec4(1, -1,  1, 1),
                vec4(1,  1,  1, 1),  vec4(-1,  1,  1, 1)
        };

        const glm::mat4 invMVP = glm::inverse(mvp);

        for (int i = 0; i != 8; i++) {
            const vec4 q = invMVP * corners[i];
            points[i] = q / q.w;
        }
    }

  public:
    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain,
                const render::RenderParams& params) override
    {
        m_table = device->createBindlessTable(128);

        rhi::ShaderModule shaderModule("cached/cull.comp", "CSMain", rhi::ShaderType::Compute);
        m_cullPass = device->createComputePipeline(shaderModule);

        std::vector<rhi::ShaderModule> modules;
        modules.emplace_back("cached/wireframe.frag", "PSMain", rhi::ShaderType::Pixel);
        modules.emplace_back("cached/wireframe.vert", "VSMain", rhi::ShaderType::Vertex);
        rhi::PipelineDesc pso;
        pso.writeDepth = true;
        pso.textureCount = 0;
        pso.fillMode = rhi::RasterFillMode::Wireframe;
        pso.topology = rhi::PrimitiveType::TriangleList;
        pso.colorAttach.emplace_back(swapChain->format());
        pso.depthAttach = rhi::Format::D32;
        m_wirePass = device->createGraphicsPipeline(modules, pso);

        rhi::TextureDesc depthDesc;
        depthDesc.debugName = "depth";
        depthDesc.isRenderTarget = true;
        depthDesc.format = rhi::Format::D32;
        depthDesc.width = swapChain->extent().width;
        depthDesc.height = swapChain->extent().height;
        m_depth = device->createTexture(depthDesc);
        rhi::CommandPtr cmd = device->createCommand(rhi::QueueType::Graphics);
        cmd->addImageBarrier(m_depth, rhi::DepthWrite);
        device->submitOneShot(cmd);

        rhi::BufferDesc desc;
        desc.isUAV = true;
        desc.isDrawIndirectArgs = true;

        desc.sizeInBytes = 16;
        desc.debugName = "countBuffer";
        m_countBuffer = device->createBuffer(desc);

        desc.stride = sizeof(render::DrawCommand);
        desc.debugName = "drawBuffer";
        desc.sizeInBytes = desc.stride * params.meshList->getInstanceCount();
        m_drawBuffer = device->createBuffer(desc);

        desc.isUAV = false;
        desc.isDrawIndirectArgs = false;
        desc.isConstantBuffer = true;
        desc.debugName = "frustumBuffer";
        desc.sizeInBytes = sizeof(render::Frustum);
        m_frustumBuffer = device->createBuffer(desc);

        desc.isReadBack = true;
        desc.isConstantBuffer = false;
        desc.debugName = "readBack";
        desc.sizeInBytes = m_frustumBuffer->sizeInBytes();
        m_readBack = device->createBuffer(desc);
        m_upload = device->createBuffer(desc.sizeInBytes, true);
        m_test = device->createBuffer(16, true);
        static std::array<uint32_t,4> clears;
        clears.fill(0u);
        m_test->uploadFromMemory(clears.data(), 16);

        meshes = params.meshList->getMeshBuffers();

        /*m_cullRes.countIndex = m_table->appendResource(m_countBuffer);
        m_cullRes.drawIndex = m_table->appendResource(m_drawBuffer);
        m_cullRes.propIndex = m_table->appendResource(params.meshList->getInstanceBuffer());
        m_cullRes.meshIndex = m_table->appendResource(meshes->getMeshBuffer());
        m_cullRes.frustIndex = m_table->appendResource(m_frustumBuffer);*/

        pass.viewport = swapChain->extent();
        pass.colors[0].loadOp = rhi::AttachmentLoadOp::Clear;
        pass.depth.loadOp = rhi::AttachmentLoadOp::Clear;
        pass.depth.texture = m_depth;
        pass.colorCount = 1;
    }
    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const render::RenderParams& params) override
    {
        uint32_t count;
        m_readBack->getUint(&count);
        ImGui::Begin("Hello ImGui", nullptr, ImGuiWindowFlags_NoResize);
        ImGui::Text("Draw count: %d", count);
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();

        render::Frustum frustum{};
        frustum.num = params.meshList->getInstanceCount();
        getFrustumPlanes(params.proj * params.view, frustum.planes);
        getFrustumCorners(params.proj * params.view, frustum.corners);
        m_upload->uploadFromMemory(&frustum, sizeof(render::Frustum));

        command->beginDebugEvent("DeferredPass", rhi::Color::Gray);

        //command->fillBuffer(m_countBuffer, 0);
        command->addBufferBarrier(m_countBuffer, rhi::CopyDest);
        command->copyBuffer(m_test, m_countBuffer, 16, 0);
        command->addBufferBarrier(m_countBuffer, rhi::UnorderedAccess);
        command->addBufferBarrier(m_drawBuffer, rhi::UnorderedAccess);

        command->addBufferBarrier(m_frustumBuffer, rhi::CopyDest);
        //command->addBufferBarrier(m_staging, rhi::CopySrc);
        command->copyBuffer(m_upload, m_frustumBuffer, m_upload->sizeInBytes(), 0);
        command->addBufferBarrier(m_frustumBuffer, rhi::ConstantBuffer);

        command->bindPipeline(m_cullPass, m_table);
        command->pushConstant(m_cullPass, rhi::ShaderType::Compute, 0, &m_cullRes, sizeof(CullResource));
        command->dispatch(1 + params.meshList->getInstanceCount() / 32, 1, 1);

        command->addBufferBarrier(m_countBuffer, rhi::CopySrc);
        //command->addBufferBarrier(m_staging, rhi::CopyDest);
        command->copyBuffer(m_countBuffer, m_readBack, m_countBuffer->sizeInBytes(), 0);
        command->addBufferBarrier(m_countBuffer, rhi::Indirect);
        command->addBufferBarrier(m_drawBuffer, rhi::Indirect);

        struct test
        {
            glm::mat4 proj = glm::mat4(1);
            glm::mat4 view = glm::mat4(1);
            glm::uint bound = 0;
        };

        test data;
        data.proj = params.proj;
        data.view = params.view;
        data.bound = m_cullRes.propIndex;

        command->bindPipeline(m_wirePass, m_table);
        command->pushConstant(m_wirePass, rhi::ShaderType::Vertex, 0, &data, sizeof(test));

        pass.colors[0].texture = backBuffer;
        command->beginRendering(pass);
        meshes->bind(command, false);
        command->drawIndirectIndexedPrimitives(m_wirePass, m_drawBuffer, m_countBuffer, params.meshList->getInstanceCount(),
                                     sizeof(render::DrawCommand));
        command->endRendering();
        command->endDebugEvent();
    }
    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override
    {
    }
    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {
    }
    void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport) override
    {
    }
};
} // namespace ler::pass
