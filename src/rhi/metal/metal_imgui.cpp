//
// Created by Loulfy on 31/12/2024.
//

#include "rhi/metal.hpp"

#define IMGUI_IMPL_METAL_CPP
#include <imgui_impl_glfw.h>
#include <imgui_impl_metal.h>

namespace ler::rhi::metal
{
ImGuiPass::~ImGuiPass()
{
    ImGui_ImplMetal_Shutdown();
}

void ImGuiPass::create(const DevicePtr& device, const SwapChainPtr& swapChain)
{
    const auto* dev = checked_cast<Device*>(device.get());
    const MetalContext& context = dev->getContext();
    ImGui_ImplMetal_Init(context.device);
}

void ImGuiPass::begin(TexturePtr& backBuffer)
{
    const auto* img = checked_cast<Texture*>(backBuffer.get());

    m_renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPassColorAttachmentDescriptor* cd = m_renderPassDescriptor->colorAttachments()->object(0);
    cd->setTexture(img->handle);
    cd->setLoadAction(MTL::LoadActionClear);
    cd->setClearColor(MTL::ClearColor(41.0f/255.0f, 42.0f/255.0f, 48.0f/255.0f, 1.0));
    cd->setStoreAction(MTL::StoreActionStore);

    ImGui_ImplMetal_NewFrame(m_renderPassDescriptor);

    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    bool show_demo_window = true;
    ImGui::ShowDemoWindow(&show_demo_window);
}

void ImGuiPass::render(TexturePtr& backBuffer, CommandPtr& command)
{
    ImGui::Render();
    // Record dear ImGui primitives into command buffer
    ImDrawData* draw_data = ImGui::GetDrawData();

    RenderingInfo pass;
    pass.viewport = backBuffer->extent();
    pass.colors[0].texture = backBuffer;
    pass.colors[0].loadOp = AttachmentLoadOp::Load;
    pass.colorCount = 1;

    auto* cmd = checked_cast<Command*>(command.get());


    MTL::RenderCommandEncoder* renderCommandEncoder = cmd->cmdBuf->renderCommandEncoder(m_renderPassDescriptor);

    command->beginDebugEvent("ImGuiPass", Color::Red);
    command->beginRendering(pass);
    ImGui_ImplMetal_RenderDrawData(draw_data, cmd->cmdBuf, renderCommandEncoder);
    renderCommandEncoder->endEncoding();
    command->endRendering();
    command->endDebugEvent();
}
} // namespace ler::rhi::metal