#include <iostream>

#include "app/desktop.hpp"
#include "pass/culling_command.hpp"
#include "pass/forward_indexed.hpp"
#include "pass/deferred_scene.hpp"
#include "render/pass.hpp"
#include "scene/scene.hpp"

#include "editor.hpp"

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

    static constexpr uint32_t kTexCount = 128;

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
        //pipeline = device->createGraphicsPipeline(modules, pso);
        pipeline = device->loadPipeline("quad", modules, pso);

        sampler = device->createSampler({ false });
        rhi::StoragePtr storage = device->getStorage();

        table = device->createBindlessTable(kTexCount);
        table->setSampler(sampler, 0);

        latch = std::make_shared<coro::latch>(1);
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "grid01.KTX2");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "Wood_BaseColor.dds");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "sponza" / "332936164838540657.DDS");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "Textures" / "Pavement_Cobble_Leaves_BLENDSHADER_Normal.dds");
        //auto files = storage->openFiles(sys::ASSETS_DIR, ".dds");
        auto files = storage->openFiles(R"(C:\Users\loria\Documents\gdev\assets\Textures)", ".dds");
        storage->requestLoadTexture(*latch, table, std::span(files.data(), kTexCount));
        //async::sync_wait(*latch);
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
            texId++;
        if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
            texId--;

        texId = std::clamp(texId, 0u, kTexCount - 1);
        command->bindPipeline(pipeline, table);
        command->pushConstant(pipeline, rhi::ShaderType::Pixel, &texId, sizeof(uint32_t));

        rhi::RenderingInfo pass;
        pass.viewport = backBuffer->extent();
        pass.colors[0].texture = backBuffer;
        pass.colorCount = 1;

        command->beginRendering(pass);
        command->drawIndexed(4);
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
        table->setResource(meshList.getInstanceBuffer(), 0);

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

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const render::RenderParams& params) override
    {
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
            //glm::uint bound = 0;
        };

        test data;
        data.proj = params.proj;
        data.view = params.view;

        command->bindPipeline(pipeline, table);
        command->pushConstant(pipeline, rhi::ShaderType::Vertex, &data, sizeof(test));

        command->beginRendering(pass);
        meshBuffers.bind(command, false);
        for(uint32_t i = 0; i < meshList.getInstanceCount(); ++i)
        {
            const render::DrawInstance& draw = meshList.getInstance(i);
            const render::IndexedMesh& mesh = meshList.getMesh(draw.meshIndex);
            command->drawIndexedInstanced(mesh.countIndex, mesh.firstIndex, mesh.firstVertex, i);
        }
        //command->drawIndirectIndexed(pipeline, drawsBuffer, countBuffer, meshList.getInstanceCount(), sizeof(render::DrawCommand));
        command->endRendering();
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {

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
    cfg.api = rhi::GraphicsAPI::D3D12;
    cfg.vsync = true;
    app::DesktopApp app(cfg);
    //app.loadScene("scene.mesh");
    //app.addPass<pass::DeferredScene>();

    // app.addPass<TestNode>();
    // app.addPass<HelloTriangle>();
    // app.addPass<TestLoadMultiTex>();
    // app.addPass<TestLoadOneTex>();
    // app.addPass<TestForward>();
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
