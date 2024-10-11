//
// Created by loulfy on 11/10/2024.
//

#include "rhi/vulkan.hpp"

namespace ler::rhi::vulkan
{
rhi::PipelinePtr Device::loadPipeline(const std::string& name, const PipelineDesc& desc)
{
    return nullptr;
}

PSOLibrary::~PSOLibrary()
{

}

PSOLibrary::PSOLibrary(Device* device)
{
    const VulkanContext& context = device->getContext();

    vk::PipelineBinaryKeyKHR globalKey;
    vk::Result res = context.device.getPipelineKeyKHR(nullptr, &globalKey);

    vk::PipelineLibraryCreateInfoKHR createInfo;
}
} // namespace ler::rhi::vulkan