//
// Created by loulfy on 11/10/2024.
//

#include "rhi/vulkan.hpp"

namespace ler::rhi::vulkan
{
rhi::PipelinePtr Device::loadPipeline(const std::string& name, const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc)
{
    // Need: MESA 24.3, Nvidia 553 ?
    log::warn("Cache Not Supported, drivers not ready!");
    bool compute = std::ranges::any_of(shaderModules, [](const ShaderModule& s){ return s.stage == ShaderType::Compute; });
    if(compute)
        return createComputePipeline(shaderModules.front());
    else
        return createGraphicsPipeline(shaderModules, desc);
}

PSOLibrary::PSOLibrary(Device* device) : m_device(device)
{
}
} // namespace ler::rhi::vulkan