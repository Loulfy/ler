//
// Created by Loulfy on 21/09/2024.
//

#include "rhi/vulkan.hpp"

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace ler::rhi::vulkan
{
ImGuiPass::~ImGuiPass()
{
    ImGui_ImplVulkan_Shutdown();
}

void ImGuiPass::create(const DevicePtr& device, const SwapChainPtr& swapChain)
{
    const auto* dev = checked_cast<Device*>(device.get());
    const VulkanContext& context = dev->getContext();
    vk::PipelineRenderingCreateInfoKHR rendering;
    const vk::Format format = Device::convertFormat(swapChain->format());
    std::initializer_list<vk::Format> formats = { format };
    rendering.setColorAttachmentFormats(formats);

    std::array<vk::DescriptorPoolSize, 11> pool_sizes = { { { vk::DescriptorType::eSampler, 1000 },
                                                            { vk::DescriptorType::eCombinedImageSampler, 1000 },
                                                            { vk::DescriptorType::eSampledImage, 1000 },
                                                            { vk::DescriptorType::eStorageImage, 1000 },
                                                            { vk::DescriptorType::eUniformTexelBuffer, 1000 },
                                                            { vk::DescriptorType::eStorageTexelBuffer, 1000 },
                                                            { vk::DescriptorType::eUniformBuffer, 1000 },
                                                            { vk::DescriptorType::eStorageBuffer, 1000 },
                                                            { vk::DescriptorType::eUniformBufferDynamic, 1000 },
                                                            { vk::DescriptorType::eStorageBufferDynamic, 1000 },
                                                            { vk::DescriptorType::eInputAttachment, 1000 } } };

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    poolInfo.setPoolSizes(pool_sizes);
    poolInfo.setMaxSets(1000);
    m_descriptorPool = context.device.createDescriptorPoolUnique(poolInfo);

    // This initializes ImGui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = context.instance;
    init_info.PhysicalDevice = context.physicalDevice;
    init_info.Device = context.device;
    init_info.Queue = context.device.getQueue(context.graphicsQueueFamily, 0);
    init_info.DescriptorPool = m_descriptorPool.get();
    init_info.PipelineRenderingCreateInfo = rendering;
    init_info.UseDynamicRendering = true;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.Subpass = 0;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&init_info);
}

void ImGuiPass::begin(TexturePtr& backBuffer)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
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

    command->beginRendering(pass);
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd->cmdBuf);
    command->endRendering();
}
} // namespace ler::rhi::vulkan