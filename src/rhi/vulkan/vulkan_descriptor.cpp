//
// Created by loulfy on 13/09/2024.
//

#include "rhi/vulkan.hpp"

namespace ler::rhi::vulkan
{
BindlessTable::BindlessTable(const VulkanContext& context, uint32_t count) : m_context(context)
{
    std::vector<vk::DescriptorPoolSize> descriptorPoolSize;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    descriptorPoolSize.emplace_back(vk::DescriptorType::eMutableEXT, count);

    auto descriptorPoolInfo = vk::DescriptorPoolCreateInfo();
    descriptorPoolInfo.setPoolSizes(descriptorPoolSize);
    descriptorPoolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind);
    descriptorPoolInfo.setMaxSets(1);
    m_pool = m_context.device.createDescriptorPoolUnique(descriptorPoolInfo);

    /*const vk::MutableDescriptorTypeListEXT list(kTypes);
    std::vector<vk::MutableDescriptorTypeListEXT> mutable_list;

    bool useMutable = false;
    if (useMutable)
    {
        bindings.resize(1);
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eMutableEXT;
        bindings[0].descriptorCount = count;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eAll;
        bindings.emplace_back(0, vk::DescriptorType::eMutableEXT, count, vk::ShaderStageFlagBits::eAll);
        descriptorPoolSize.emplace_back(vk::DescriptorType::eMutableEXT, count);
        mutable_list.emplace_back(list);
    }
    else
    {
        bindings.resize(kTypes.size());
        mutable_list.resize(kTypes.size());
        descriptorPoolSize.resize(kTypes.size());
        for (int i = 0; i < kTypes.size(); ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = kTypes[i];
            bindings[i].descriptorCount = 16;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eAll;
            descriptorPoolSize[i].type = kTypes[i];
            descriptorPoolSize[i].descriptorCount = count;
        }
    }

    auto descriptorPoolInfo = vk::DescriptorPoolCreateInfo();
    descriptorPoolInfo.setPoolSizes(descriptorPoolSize);
    descriptorPoolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind);
    descriptorPoolInfo.setMaxSets(1);
    m_pool = m_context.device.createDescriptorPoolUnique(descriptorPoolInfo);

    vk::DescriptorSetLayoutCreateInfo descriptorLayoutInfo;
    descriptorLayoutInfo.setBindings(bindings);

    vk::MutableDescriptorTypeCreateInfoEXT mutable_info;
    mutable_info.setMutableDescriptorTypeLists(mutable_list);

    vk::DescriptorSetLayoutBindingFlagsCreateInfo extended_info;
    vk::DescriptorBindingFlags bindless_flags =
        vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    std::vector<vk::DescriptorBindingFlags> binding_flags(descriptorLayoutInfo.bindingCount, bindless_flags);
    extended_info.setBindingFlags(binding_flags);
    extended_info.setPNext(&mutable_info);

    descriptorLayoutInfo.setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool);
    descriptorLayoutInfo.setPNext(&extended_info);

    vk::DescriptorSetLayoutSupport support;
    m_context.device.getDescriptorSetLayoutSupport(&descriptorLayoutInfo, &support);
    if (!support.supported)
        log::exit("Mutable DescriptorSet not supported");
    m_layout = m_context.device.createDescriptorSetLayoutUnique(descriptorLayoutInfo);*/

    vk::DescriptorSetAllocateInfo descriptorSetAllocInfo;
    descriptorSetAllocInfo.setDescriptorSetCount(1);
    descriptorSetAllocInfo.setDescriptorPool(m_pool.get());
    descriptorSetAllocInfo.setPSetLayouts(&m_context.bindlessLayout);
    vk::Result res = m_context.device.allocateDescriptorSets(&descriptorSetAllocInfo, &m_descriptor);
    assert(res == vk::Result::eSuccess);
}

vk::UniqueDescriptorSetLayout BindlessTable::buildBindlessLayout(const VulkanContext& context, uint32_t count,
                                                                 bool useMutable)
{
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    std::vector<vk::DescriptorPoolSize> descriptorPoolSize;

    const vk::MutableDescriptorTypeListEXT list(kStandard);
    std::vector<vk::MutableDescriptorTypeListEXT> mutable_list;

    const std::initializer_list<vk::DescriptorType> types = useMutable ? kMutable : kStandard;
    bindings.resize(types.size());
    mutable_list.resize(types.size());
    descriptorPoolSize.resize(types.size());
    int i = 0;
    for (const vk::DescriptorType& type : types)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = type;
        bindings[i].descriptorCount = count;
        bindings[i].stageFlags = vk::ShaderStageFlagBits::eAll;
        descriptorPoolSize[i].type = type;
        descriptorPoolSize[i].descriptorCount = count;
        if (type == vk::DescriptorType::eMutableEXT)
            mutable_list[i] = list;
        i += 1;
    }

    log::debug("BindlessLayout:");
    for (auto& b : bindings)
        log::debug("set = 0, binding = {}, count = {:02}, type = {}", b.binding, b.descriptorCount,
                   vk::to_string(b.descriptorType));
    log::debug("======================================================");

    vk::DescriptorSetLayoutCreateInfo descriptorLayoutInfo;
    descriptorLayoutInfo.setBindings(bindings);

    vk::MutableDescriptorTypeCreateInfoEXT mutable_info;
    mutable_info.setMutableDescriptorTypeLists(mutable_list);

    vk::DescriptorSetLayoutBindingFlagsCreateInfo extended_info;
    vk::DescriptorBindingFlags bindless_flags =
        vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    std::vector<vk::DescriptorBindingFlags> binding_flags(descriptorLayoutInfo.bindingCount, bindless_flags);
    extended_info.setBindingFlags(binding_flags);
    extended_info.setPNext(&mutable_info);

    descriptorLayoutInfo.setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool);
    descriptorLayoutInfo.setPNext(&extended_info);

    vk::DescriptorSetLayoutSupport support;
    context.device.getDescriptorSetLayoutSupport(&descriptorLayoutInfo, &support);
    if (!support.supported)
        log::exit("Mutable DescriptorSet not supported");
    return context.device.createDescriptorSetLayoutUnique(descriptorLayoutInfo);
}

void BindlessTable::setSampler(const SamplerPtr& sampler, uint32_t slot)
{
    auto* native = checked_cast<Sampler*>(sampler.get());
    auto type = vk::DescriptorType::eSampler;

    vk::DescriptorImageInfo imageInfo;
    imageInfo.setSampler(native->handle.get());

    vk::WriteDescriptorSet descriptorWriteInfo;
    descriptorWriteInfo.setDescriptorType(type);
    descriptorWriteInfo.setDstBinding(1);
    descriptorWriteInfo.setDescriptorCount(1);
    descriptorWriteInfo.setDstSet(m_descriptor);
    descriptorWriteInfo.setDstArrayElement(slot);
    descriptorWriteInfo.setImageInfo(imageInfo);

    m_context.device.updateDescriptorSets(descriptorWriteInfo, nullptr);
}

bool BindlessTable::visitTexture(const TexturePtr& texture, uint32_t slot)
{
    auto* image = checked_cast<Texture*>(texture.get());

    auto type = vk::DescriptorType::eSampledImage;

    vk::DescriptorImageInfo imageInfo;
    imageInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    imageInfo.setImageView(image->view());

    vk::WriteDescriptorSet descriptorWriteInfo;
    descriptorWriteInfo.setDescriptorType(type);
    descriptorWriteInfo.setDstBinding(0);
    descriptorWriteInfo.setDescriptorCount(1);
    descriptorWriteInfo.setDstSet(m_descriptor);
    descriptorWriteInfo.setDstArrayElement(slot);
    descriptorWriteInfo.setImageInfo(imageInfo);

    m_context.device.updateDescriptorSets(descriptorWriteInfo, nullptr);
    return true;
}

bool BindlessTable::visitBuffer(const BufferPtr& buffer, uint32_t slot)
{
    return true;
}

BindlessTablePtr Device::createBindlessTable(uint32_t count)
{
    return std::make_shared<BindlessTable>(m_context, count);
}
} // namespace ler::rhi::vulkan