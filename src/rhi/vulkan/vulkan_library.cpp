//
// Created by loulfy on 11/10/2024.
//

#include "rhi/vulkan.hpp"
#include <spdlog/fmt/bin_to_hex.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <fmt/format.h>

template<>
struct fmt::formatter<vk::PipelineBinaryKeyKHR>
{
    constexpr auto parse(fmt::format_parse_context& ctx) {
        return ctx.begin();
    }
    auto format(const vk::PipelineBinaryKeyKHR& obj, fmt::format_context& ctx) const {
        const std::span v(obj.key.data(), obj.keySize);
        return fmt::format_to(ctx.out(), "{:x}", fmt::join(v, ""));
    }
};

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
    //m_globalKey = m_device->getContext().device.getPipelineKeyKHR(nullptr);

    //const std::span v(m_globalKey.key.data(), m_globalKey.keySize);

    //log::info("globalKey: {:x}", spdlog::to_hex(v));
    //std::string test = fmt::format("{:X}", fmt::join(v, ""));

    /*std::string test = fmt::format("{}", m_globalKey);
    std::string_view s(test);

    std::vector<uint8_t> binary(32);
    for(int i = 0; i < 32; i++)
    {
        auto ss = s.substr(i, 2);
        std::from_chars(ss.begin(), ss.end(), binary[i], 16);
    }

    vk::PipelineCreateInfoKHR pipelineCreateInfo;*/
}
} // namespace ler::rhi::vulkan