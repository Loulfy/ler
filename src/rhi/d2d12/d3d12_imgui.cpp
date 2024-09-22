//
// Created by loulfy on 22/09/2024.
//

#include "rhi/d3d12.hpp"

#include <imgui_impl_dx12.h>
#include <imgui_impl_glfw.h>

namespace ler::rhi::d3d12
{
ImGuiPass::~ImGuiPass()
{
    ImGui_ImplDX12_Shutdown();
}

void ImGuiPass::create(const DevicePtr& device, const SwapChainPtr& swapChain)
{
    auto* dev = checked_cast<Device*>(device.get());
    const D3D12Context& context = dev->getContext();
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    context.device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap));

    DXGI_FORMAT format = Device::convertFormatRtv(swapChain->format());

    // This initializes ImGui for DX12
    ImGui_ImplDX12_Init(context.device, ISwapChain::FrameCount, format, g_pd3dSrvDescHeap.Get(),
                        g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
                        g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());
}

void ImGuiPass::begin()
{
    ImGui_ImplDX12_NewFrame();
}

void ImGuiPass::render(TexturePtr& backBuffer, CommandPtr& command)
{
    ImGui::Render();
    // Record dear ImGui primitives into command buffer
    ImDrawData* draw_data = ImGui::GetDrawData();

    rhi::RenderingInfo pass;
    pass.viewport = backBuffer->extent();
    pass.colors[0].texture = backBuffer;
    pass.colors[0].loadOp = AttachmentLoadOp::Load;
    pass.colorCount = 1;

    auto* cmd = checked_cast<Command*>(command.get());

    ID3D12DescriptorHeap* heap = g_pd3dSrvDescHeap.Get();

    command->beginDebugEvent("ImGuiPass", Color::Red);
    command->beginRendering(pass);
    cmd->m_commandList->SetDescriptorHeaps(1, &heap);
    ImGui_ImplDX12_RenderDrawData(draw_data, cmd->m_commandList.Get());
    command->endRendering();
    command->endDebugEvent();
}
} // namespace ler::rhi::d3d12