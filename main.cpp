#include <iostream>

#include "app/desktop.hpp"
#include "pass/culling_command.hpp"
#include "pass/forward_indexed.hpp"
#include "pass/deferred_scene.hpp"
#include "render/pass.hpp"
#include "scene/scene.hpp"

using namespace ler;

class TestLoadOneTex final : public rhi::IRenderPass
{
  public:
    rhi::PipelinePtr pipeline;
    rhi::TexturePtr texture;
    rhi::SamplerPtr sampler;
    rhi::BindlessTablePtr table;
    rhi::Latch latch;
    uint32_t texId = 0;

    static constexpr uint32_t kTexCount = 2;

    [[nodiscard]] bool startup() const override
    {
        return latch->is_ready();
    }

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override
    {
        std::vector<rhi::ShaderModule> modules;
        modules.emplace_back("cached/quad.frag", "PSMain", rhi::ShaderType::Pixel);
        modules.emplace_back("cached/quad.vert", "VSMain", rhi::ShaderType::Vertex);
        rhi::PipelineDesc pso;
        pso.writeDepth = false;
        pso.textureCount = 0;
        pso.topology = rhi::PrimitiveType::TriangleStrip;
        pso.colorAttach.emplace_back(swapChain->format());
        pipeline = device->createGraphicsPipeline(modules, pso);
        //pipeline = device->loadPipeline("quad", modules, pso);

        rhi::SamplerDesc sd;
        sd.filter = false;
        sd.addressU = rhi::SamplerAddressMode::Repeat;
        sd.addressV = rhi::SamplerAddressMode::Repeat;
        sd.addressW = rhi::SamplerAddressMode::Repeat;
        sampler = device->createSampler(sd);
        rhi::StoragePtr storage = device->getStorage();

        table = device->createBindlessTable(128);
        table->setSampler(sampler, 0);

        latch = std::make_shared<coro::latch>(1);
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "grid01.KTX2");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "Wood_BaseColor.dds");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "sponza" / "332936164838540657.DDS");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "Textures" / "Pavement_Cobble_Leaves_BLENDSHADER_Normal.dds");
        auto files = storage->openFiles(sys::ASSETS_DIR, ".dds");
        //auto files = storage->openFiles(R"(/Users/lcorbel/Downloads/Bistro_v5_2/Textures)", ".dds");
        storage->requestLoadTexture(*latch, table, std::span(files.data(), kTexCount));
        async::sync_wait(*latch);
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
            texId++;
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
            texId--;

        texId = std::clamp(texId, 0u, kTexCount - 1);

        rhi::RenderingInfo pass;
        pass.viewport = backBuffer->extent();
        pass.colors[0].texture = backBuffer;
        pass.colorCount = 1;

        command->beginRendering(pass);
        command->bindPipeline(pipeline, table);
        command->pushConstant(pipeline, rhi::ShaderType::Pixel, 0, &texId, sizeof(uint32_t));
        command->drawPrimitives(4);
        command->endRendering();

        ImGui::Begin("Hello ImGui", nullptr, ImGuiWindowFlags_NoResize);
        ImGui::Text("Texture Index: %d", texId);
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }
};

class TestForward final : public rhi::IRenderPass, public render::IMeshRenderer
{
  public:
    rhi::PipelinePtr pipeline;
    rhi::SamplerPtr sampler;
    rhi::BindlessTablePtr table;
    rhi::TexturePtr depth;
    render::MeshBuffers meshBuffers;
    render::RenderMeshList meshList;

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override
    {
        const std::vector<char> blob = sys::readBlobFile(sys::ASSETS_DIR / "scene.mesh");

        flatbuffers::Verifier v(reinterpret_cast<const uint8_t* const>(blob.data()), blob.size());
        assert(scene::VerifySceneBuffer(v));
        auto scene = scene::GetScene(blob.data());

        meshBuffers.allocate(device, *scene->buffers());
        meshBuffers.load(*scene->meshes());
        meshList.installStaticScene(device, *scene->instances());
        meshList.setMeshBuffers(&meshBuffers);

        table = device->createBindlessTable(128);
        table->appendResource(meshList.getInstanceBuffer());

        std::vector<rhi::ShaderModule> modules;
        modules.emplace_back("cached/wireframe.frag", "main", rhi::ShaderType::Pixel);
        modules.emplace_back("cached/wireframe.vert", "main", rhi::ShaderType::Vertex);
        rhi::PipelineDesc pso;
        pso.writeDepth = true;
        pso.textureCount = 0;
        pso.fillMode = rhi::RasterFillMode::Fill;
        pso.topology = rhi::PrimitiveType::TriangleList;
        pso.colorAttach.emplace_back(swapChain->format());
        pso.depthAttach = rhi::Format::D32;
        pipeline = device->createGraphicsPipeline(modules, pso);

        rhi::TextureDesc depthDesc;
        depthDesc.debugName = "depth";
        depthDesc.isRenderTarget = true;
        depthDesc.format = rhi::Format::D32;
        depthDesc.width = swapChain->extent().width;
        depthDesc.height = swapChain->extent().height;
        depth = device->createTexture(depthDesc);
        auto cmd = device->createCommand(rhi::QueueType::Graphics);
        cmd->addImageBarrier(depth, ler::rhi::DepthWrite);
        device->submitOneShot(cmd);

        auto storage = device->getStorage();
        rhi::Latch latch = std::make_shared<coro::latch>(1);
        auto files = storage->openFiles(sys::ASSETS_DIR, ".dds");
        storage->requestLoadTexture(*latch, table, std::span(files.data(), 1));
        async::sync_wait(*latch);

        rhi::SamplerDesc sd;
        sd.filter = false;
        //sd.addressU = rhi::SamplerAddressMode::Repeat;
        //sd.addressV = rhi::SamplerAddressMode::Repeat;
        //sd.addressW = rhi::SamplerAddressMode::Repeat;
        //sampler = device->createSampler(sd);
        //table->setSampler(sampler, 0);
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const render::RenderParams& params) override
    {
        ImGui::Begin("Hello MoltenVK", nullptr, ImGuiWindowFlags_NoResize);
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();

        rhi::RenderingInfo pass;
        pass.viewport = backBuffer->extent();
        pass.colors[0].loadOp = rhi::AttachmentLoadOp::Clear;
        pass.colors[0].texture = backBuffer;
        pass.depth.loadOp = rhi::AttachmentLoadOp::Clear;
        pass.depth.texture = depth;
        pass.colorCount = 1;

        struct test
        {
            glm::mat4 proj = glm::mat4(1);
            glm::mat4 view = glm::mat4(1);
            glm::uint bound = 0;
        };

        test data;
        data.proj = params.proj;
        data.view = params.view;

        command->beginRendering(pass);
        command->bindPipeline(pipeline, table);
        command->pushConstant(pipeline, rhi::ShaderType::Vertex, 0, &data, sizeof(test));
        meshBuffers.bind(command, false);
        for(uint32_t i = 0; i < meshList.getInstanceCount(); ++i)
        {
            const render::DrawInstance& draw = meshList.getInstance(i);
            const render::IndexedMesh& mesh = meshList.getMesh(draw.meshIndex);
            command->drawIndexedPrimitives(mesh.countIndex, mesh.firstIndex, mesh.firstVertex, i);
        }
        //command->drawIndirectIndexed(pipeline, drawsBuffer, countBuffer, meshList.getInstanceCount(), sizeof(render::DrawCommand));
        command->endRendering();
    }

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain, const render::RenderParams& params) override
    {

    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {

    }
};

class TestIndirect final : public rhi::IRenderPass, public render::IMeshRenderer
{
  public:
    rhi::PipelinePtr pipeline;
    rhi::SamplerPtr sampler;
    rhi::BindlessTablePtr table;
    rhi::TexturePtr depth;
    render::MeshBuffers meshBuffers;
    render::RenderMeshList meshList;

    struct CullIndex
    {
        uint32_t propIndex;
        uint32_t meshIndex;
        uint32_t drawIndex;
        uint32_t countIndex;
        uint32_t frustIndex;
    };

    rhi::PipelinePtr cullPass;
    rhi::BufferPtr drawsBuffer;
    rhi::BufferPtr countBuffer;
    rhi::BufferPtr frustBuffer;
    rhi::BufferPtr staging;
    rhi::BufferPtr clearer;
    rhi::BufferPtr readback;
    CullIndex cullRes;

    rhi::BufferPtr drawConstant;
    rhi::BufferPtr cullConstant;

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override
    {
        const std::vector<char> blob = sys::readBlobFile(sys::ASSETS_DIR / "scene.mesh");

        flatbuffers::Verifier v(reinterpret_cast<const uint8_t* const>(blob.data()), blob.size());
        assert(scene::VerifySceneBuffer(v));
        auto scene = scene::GetScene(blob.data());

        meshBuffers.allocate(device, *scene->buffers());
        meshBuffers.load(*scene->meshes());
        meshBuffers.allocate(device, table, *scene->materials());
        meshList.installStaticScene(device, *scene->instances());
        meshList.setMeshBuffers(&meshBuffers);

        table = device->createBindlessTable(128);
        //table->setResource(meshList.getInstanceBuffer(), 0);

        rhi::ShaderModule module("cached/cullmesh.comp", "CSMain", rhi::ShaderType::Compute);
        cullPass = device->createComputePipeline(module);

        std::vector<rhi::ShaderModule> modules;
        modules.emplace_back("cached/indirectmesh.frag", "PSMain", rhi::ShaderType::Pixel);
        modules.emplace_back("cached/indirectmesh.vert", "VSMain", rhi::ShaderType::Vertex);
        rhi::PipelineDesc pso;
        pso.writeDepth = true;
        pso.textureCount = 0;
        pso.fillMode = rhi::RasterFillMode::Wireframe;
        pso.topology = rhi::PrimitiveType::TriangleList;
        pso.colorAttach.emplace_back(swapChain->format());
        pso.depthAttach = rhi::Format::D32;
        pso.indirectDraw = true;
        pipeline = device->createGraphicsPipeline(modules, pso);

        rhi::TextureDesc depthDesc;
        depthDesc.debugName = "depth";
        depthDesc.isRenderTarget = true;
        depthDesc.format = rhi::Format::D32;
        depthDesc.width = swapChain->extent().width;
        depthDesc.height = swapChain->extent().height;
        depth = device->createTexture(depthDesc);
        auto cmd = device->createCommand(rhi::QueueType::Graphics);
        cmd->addImageBarrier(depth, ler::rhi::DepthWrite);
        device->submitOneShot(cmd);
    }

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

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const render::RenderParams& params) override
    {
        command->addBufferBarrier(countBuffer, rhi::CopySrc);
        command->copyBuffer(countBuffer, readback, 256, 0);
        command->addBufferBarrier(countBuffer, rhi::CopyDest);
        command->copyBuffer(clearer, countBuffer, 256, 0);
        uint32_t num;
        readback->getUint(&num);
        ImGui::Begin("Hello LER", nullptr, ImGuiWindowFlags_NoResize);
        ImGui::Text("Culled Instance: %d", num);
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();

        render::Frustum f;
        getFrustumCorners(params.proj * params.view, f.corners);
        getFrustumPlanes(params.proj * params.view, f.planes);
        f.num = meshList.getInstanceCount();
        staging->uploadFromMemory(&f, sizeof(render::Frustum));
        command->addBufferBarrier(frustBuffer, rhi::CopyDest);
        command->copyBuffer(staging, frustBuffer, sizeof(render::Frustum), 0);
        //command->syncBuffer(cullConstant, &cullRes, sizeof(CullIndex));
        cullConstant->uploadFromMemory(&cullRes, sizeof(CullIndex));

        command->bindPipeline(cullPass, table);
        command->pushConstant(cullPass, rhi::ShaderType::Compute, 0, &cullRes, sizeof(CullIndex));
        //command->setConstant(cullConstant, rhi::ShaderType::Compute);

        // drawsBuff + ICB
        // index
        // vertex
        // constant address
        // descHeap

        command->addBufferBarrier(meshList.getInstanceBuffer(), rhi::ShaderResource);
        command->addBufferBarrier(meshBuffers.getMeshBuffer(), rhi::ShaderResource);
        command->addBufferBarrier(frustBuffer, rhi::ConstantBuffer);
        command->addBufferBarrier(countBuffer, rhi::UnorderedAccess);
        command->addBufferBarrier(drawsBuffer, rhi::UnorderedAccess);
        command->dispatch(1 + meshList.getInstanceCount() / 32, 1, 1);
        command->addBufferBarrier(countBuffer, rhi::Indirect);
        command->addBufferBarrier(drawsBuffer, rhi::Indirect);

        rhi::RenderingInfo pass;
        pass.viewport = backBuffer->extent();
        pass.colors[0].loadOp = rhi::AttachmentLoadOp::Clear;
        pass.colors[0].texture = backBuffer;
        pass.depth.loadOp = rhi::AttachmentLoadOp::Clear;
        pass.depth.texture = depth;
        pass.colorCount = 1;

        render::DrawConstant data;
        data.proj = params.proj;
        data.view = params.view;
        //command->syncBuffer(drawConstant, &data, sizeof(render::DrawConstant));
        drawConstant->uploadFromMemory(&data, sizeof(render::DrawConstant));

        rhi::EncodeIndirectIndexedDrawDesc encoder;
        meshBuffers.bind(encoder, true);
        encoder.maxDrawCount = meshList.getInstanceCount();
        encoder.constantBuffer = drawConstant;
        encoder.drawsBuffer = drawsBuffer;
        encoder.countBuffer = countBuffer;
        encoder.table = table;
        command->encodeIndirectIndexedPrimitives(encoder);

        command->beginRendering(pass);
        command->bindPipeline(pipeline, table);
        //command->setConstant(drawConstant, rhi::ShaderType::Vertex);
        command->pushConstant(pipeline, rhi::ShaderType::Vertex, 0, &data, sizeof(render::DrawConstant));
        meshBuffers.bind(command, true);

        /*for(uint32_t i = 0; i < meshList.getInstanceCount(); ++i)
        {
            data.index = i;
            command->pushConstant(pipeline, rhi::ShaderType::Vertex, i + 2, &data, sizeof(test));
            const render::DrawInstance& draw = meshList.getInstance(i);
            const render::IndexedMesh& mesh = meshList.getMesh(draw.meshIndex);
            command->drawIndexedInstanced(mesh.countIndex, mesh.firstIndex, mesh.firstVertex, i);
        }*/
        command->drawIndirectIndexedPrimitives(pipeline, drawsBuffer, countBuffer, meshList.getInstanceCount(), sizeof(render::DrawCommand));
        command->endRendering();
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {

    }

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain,
                const render::RenderParams& params) override
    {
        rhi::BufferDesc buffDesc;

        //buffDesc.isConstantBuffer = true;
        buffDesc.debugName = "frustBuffer";
        buffDesc.stride = sizeof(render::Frustum);
        buffDesc.byteSize = sizeof(render::Frustum);
        buffDesc.isConstantBuffer = true;
        frustBuffer = device->createBuffer(buffDesc);

        staging = device->createBuffer(buffDesc.byteSize, true);
        clearer = device->createBuffer(256, true);
        std::array<uint32_t, 64> values = {};
        values.fill(1);
        values[0] = 0;
        values[1] = 0;
        clearer->uploadFromMemory(values.data(), 256);
        rhi::BufferDesc rDesc;
        rDesc.isReadBack = true;
        rDesc.byteSize = 256;
        readback = device->createBuffer(rDesc);
        auto cmd = device->createCommand(rhi::QueueType::Graphics);
        cmd->addBufferBarrier(readback, rhi::CopyDest);
        cmd->addBufferBarrier(clearer, rhi::CopySrc);
        cmd->addBufferBarrier(staging, rhi::CopySrc);
        device->submitOneShot(cmd);

        buffDesc.isUAV = true;
        buffDesc.isDrawIndirectArgs = true;
        buffDesc.isConstantBuffer = false;

        buffDesc.debugName = "drawsBuffer";
        buffDesc.isICB = true;
        buffDesc.stride = sizeof(render::DrawCommand);
        buffDesc.byteSize = sizeof(render::DrawCommand) * meshList.getInstanceCount();
        drawsBuffer = device->createBuffer(buffDesc);

        buffDesc.debugName = "countBuffer";
        buffDesc.isICB = false;
        buffDesc.stride = 0;
        buffDesc.byteSize = 256;
        buffDesc.format = rhi::Format::R32_UINT;
        countBuffer = device->createBuffer(buffDesc);

        buffDesc.format = rhi::Format::UNKNOWN;
        buffDesc.debugName = "drawConstant";
        //buffDesc.isManaged = true;
        buffDesc.isUAV = false;
        buffDesc.isStaging = true;
        buffDesc.stride = sizeof(render::DrawConstant);
        buffDesc.byteSize = sizeof(render::DrawConstant);
        drawConstant = device->createBuffer(buffDesc);

        buffDesc.debugName = "cullConstant";
        cullConstant = device->createBuffer(buffDesc);

        cullRes.propIndex = table->appendResource(meshList.getInstanceBuffer());
        cullRes.meshIndex = table->appendResource(meshBuffers.getMeshBuffer());
        cullRes.drawIndex = table->appendResource(drawsBuffer);
        cullRes.countIndex = table->appendResource(countBuffer);
        cullRes.frustIndex = table->appendResource(frustBuffer);
    }
};

int main()
{
    // ler::scene::SceneImporter::preparator(R"(C:\Users\loria\Downloads\Bistro_v5_2\Bistro_v5_2\BistroExterior.fbx)");
    // ler::scene::SceneImporter::preparator(R"(C:\Users\loria\Documents\EnhancedLER\assets\sponza.glb)");
    // ler::scene::SceneImporter::preparator(R"(C:\Users\loria\Documents\minimalRT\assets\sponza\Sponza.gltf)");

    app::AppConfig cfg;
    cfg.debug = true;
    cfg.width = 1080;
    cfg.height = 720;
    cfg.api = rhi::GraphicsAPI::METAL;
    cfg.vsync = true;
    app::DesktopApp app(cfg);
    //app.loadScene("scene.mesh");
    //app.addPass<pass::DeferredScene>();

    // app.addPass<TestNode>();
    // app.addPass<HelloTriangle>();
    // app.addPass<TestLoadMultiTex>();
    // app.addPass<TestLoadOneTex>();
    app.addPass<TestIndirect>();
    // app.renderGraph().parse("wireframe.json");
    // app.renderGraph().addPass<pass::ForwardIndexed>();
    // app.renderGraph().addPass<pass::CullingCommand>();

    //render::RenderGraphPtr graph = app.addPass<render::RenderGraph>();
    //graph->parse("test_graph.json");
    // graph->addPass<pass::ForwardIndexed>();
    //graph->addPass<pass::CullingCommand>();
    app.run();

    return EXIT_SUCCESS;
}
