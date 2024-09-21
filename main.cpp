#include <iostream>

#include "app/desktop.hpp"
#include "pass/culling_command.hpp"
#include "pass/forward_indexed.hpp"
#include "scene/scene.hpp"
#include "sys/ioring.hpp"

using namespace ler;

class TestLoadOneTex final : public rhi::IRenderPass
{
  public:
    rhi::PipelinePtr pipeline;
    rhi::TexturePtr texture;
    rhi::SamplerPtr sampler;
    rhi::BindlessTablePtr table;
    rhi::Latch latch;

    [[nodiscard]] bool startup() const override
    {
        return latch->is_ready();
    }

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override
    {
        std::vector<rhi::ShaderModule> modules;
        modules.emplace_back("cached/pouet.frag", "PSMain", rhi::ShaderType::Pixel);
        modules.emplace_back("cached/quad.vert", "VSMain", rhi::ShaderType::Vertex);
        rhi::PipelineDesc pso;
        pso.writeDepth = false;
        pso.textureCount = 0;
        pso.topology = rhi::PrimitiveType::TriangleStrip;
        pso.colorAttach.emplace_back(swapChain->format());
        pipeline = device->createGraphicsPipeline(modules, pso);

        sampler = device->createSampler({ false });
        rhi::StoragePtr storage = device->getStorage();

        table = device->createBindlessTable(16);
        table->setSampler(sampler, 0);

        latch = std::make_shared<coro::latch>(1);
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "grid01.KTX2");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "Wood_BaseColor.dds");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "sponza" / "332936164838540657.DDS");
        //rhi::ReadOnlyFilePtr file = storage->openFile(sys::ASSETS_DIR / "Textures" / "Pavement_Cobble_Leaves_BLENDSHADER_Normal.dds");
        //auto files = storage->openFiles(sys::ASSETS_DIR, ".dds");
        auto files = storage->openFiles(R"(C:\Users\pouet\Documents\gdev\assets\Textures)", ".dds");
        storage->requestLoadTexture(*latch, table, std::span(files.data(), 16));
        //async::sync_wait(*latch);
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {
        uint32_t tex = 12;

        command->bindPipeline(pipeline, table);
        command->pushConstant(pipeline, &tex, sizeof(uint32_t));

        rhi::RenderingInfo pass;
        pass.viewport = backBuffer->extent();
        pass.colors[0].texture = backBuffer;
        pass.colorCount = 1;

        command->beginRendering(pass);
        command->drawIndexed(4);
        command->endRendering();
    }
};

class TestLoadMultiTex final : public rhi::IRenderPass
{
  public:
    rhi::PipelinePtr pipeline;
    rhi::BindlessTablePtr table;
    rhi::SamplerPtr sampler;
    rhi::Latch latch;

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override
    {
        std::vector<rhi::ShaderModule> modules;
        modules.emplace_back("cached/quad.frag", "PSMain", rhi::ShaderType::Pixel);
        modules.emplace_back("cached/quad.vert", "VSMain", rhi::ShaderType::Vertex);
        rhi::PipelineDesc pso;
        pso.writeDepth = false;
        pso.topology = rhi::PrimitiveType::TriangleStrip;
        pso.colorAttach.emplace_back(swapChain->format());
        pipeline = device->createGraphicsPipeline(modules, pso);

        sampler = device->createSampler({ false });
        rhi::StoragePtr storage = device->getStorage();

        latch = std::make_shared<coro::latch>(1);

        table = device->createBindlessTable(16);
        std::vector<rhi::ReadOnlyFilePtr> files = storage->openFiles(sys::ASSETS_DIR / "sponza", ".DDS");
        storage->requestLoadTexture(*latch, table, files);

        rhi::TexturePtr texture = table->getTexture(1);

        pipeline->createDescriptorSet(0);
        pipeline->updateSampler(0, 1, sampler, texture);
    }

    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override
    {
        command->bindPipeline(pipeline, 0);

        rhi::RenderingInfo pass;
        pass.viewport = backBuffer->extent();
        pass.colors[0].texture = backBuffer;
        pass.colorCount = 1;

        command->beginRendering(pass);
        command->drawIndexed(4);
        command->endRendering();
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
    cfg.api = rhi::GraphicsAPI::VULKAN;
    app::DesktopApp app(cfg);
    // app.loadScene("scene.mesh");
    // app.addPass<HelloTriangle>();
    // app.addPass<TestLoadMultiTex>();
    app.addPass<TestLoadOneTex>();
    // app.renderGraph().parse("wireframe.json");
    // app.renderGraph().addPass<pass::ForwardIndexed>();
    // app.renderGraph().addPass<pass::CullingCommand>();

    // render::RenderGraphPtr graph = app.addPass<render::RenderGraph>();
    // graph->parse("wireframe.json");
    // graph->addPass<pass::ForwardIndexed>();
    // graph->addPass<pass::CullingCommand>();
    app.run();

    return EXIT_SUCCESS;
}
