//
// Created by Loulfy on 13/09/2024.
//

#include "rhi/vulkan.hpp"

namespace ler::rhi::vulkan
{
BindlessTable::BindlessTable(const VulkanContext& context, uint32_t count)
    : CommonBindlessTable(count), m_context(context), m_bufferDescriptor(context)
{
    vk::DeviceSize layoutSize;
    m_context.device.getDescriptorSetLayoutSizeEXT(m_context.bindlessLayout, &layoutSize);
    createBufferDescriptor(m_bufferDescriptor, layoutSize);

    m_context.device.getDescriptorSetLayoutBindingOffsetEXT(m_context.bindlessLayout, 0, &m_mutableDescriptorOffset);
    m_context.device.getDescriptorSetLayoutBindingOffsetEXT(m_context.bindlessLayout, 1, &m_samplerDescriptorOffset);

    for (const vk::DescriptorType& b : kStandard)
        m_mutableDescriptorSize = std::max(m_mutableDescriptorSize, getDescriptorSizeForType(b));
}

void BindlessTable::createBufferDescriptor(Buffer& buffer, uint64_t layoutSize) const
{
    constexpr vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eShaderDeviceAddress |
                                                vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT |
                                                vk::BufferUsageFlagBits::eSamplerDescriptorBufferEXT;
    buffer.info.setSize(layoutSize);
    buffer.info.setUsage(usageFlags);
    buffer.info.setSharingMode(vk::SharingMode::eExclusive);

    buffer.allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    buffer.allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    vmaCreateBuffer(m_context.allocator, reinterpret_cast<VkBufferCreateInfo*>(&buffer.info), &buffer.allocInfo,
                    reinterpret_cast<VkBuffer*>(&buffer.handle), &buffer.allocation, &buffer.hostInfo);

    memset(buffer.hostInfo.pMappedData, 0, layoutSize);
}

uint32_t BindlessTable::getDescriptorSizeForType(vk::DescriptorType descriptorType) const
{
    const vk::PhysicalDeviceDescriptorBufferPropertiesEXT& props = m_context.descBufferProperties;
    switch (descriptorType)
    {
    case vk::DescriptorType::eSampler:
        return props.samplerDescriptorSize;
    case vk::DescriptorType::eSampledImage:
        return props.sampledImageDescriptorSize;
    case vk::DescriptorType::eStorageImage:
        return props.storageImageDescriptorSize;
    case vk::DescriptorType::eUniformTexelBuffer:
        return props.robustUniformTexelBufferDescriptorSize;
    case vk::DescriptorType::eStorageTexelBuffer:
        return props.robustStorageTexelBufferDescriptorSize;
    case vk::DescriptorType::eUniformBuffer:
        return props.robustUniformBufferDescriptorSize;
    case vk::DescriptorType::eStorageBuffer:
        return props.robustStorageBufferDescriptorSize;
    case vk::DescriptorType::eAccelerationStructureKHR:
        return props.accelerationStructureDescriptorSize;
    default:
        assert(0 && "Invalid descriptor type.");
        return 0;
    }
}

vk::UniqueDescriptorSetLayout BindlessTable::buildBindlessLayout(const VulkanContext& context, uint32_t count)
{
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

    const vk::MutableDescriptorTypeListEXT list(kStandard);
    std::vector<vk::MutableDescriptorTypeListEXT> mutableList;

    const std::initializer_list<vk::DescriptorType> types = kMutable;
    bindings.resize(types.size());
    mutableList.resize(types.size());
    int i = 0;
    for (const vk::DescriptorType& type : types)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = type;
        bindings[i].descriptorCount = count;
        bindings[i].stageFlags = vk::ShaderStageFlagBits::eAll;
        if (type == vk::DescriptorType::eMutableEXT)
            mutableList[i] = list;
        i += 1;
    }

    log::debug("BindlessLayout:");
    for (auto& b : bindings)
        log::debug("set = 0, binding = {}, count = {:04}, type = {}", b.binding, b.descriptorCount,
                   vk::to_string(b.descriptorType));
    log::debug("======================================================");

    vk::DescriptorSetLayoutCreateInfo descriptorLayoutInfo;
    descriptorLayoutInfo.setBindings(bindings);

    vk::MutableDescriptorTypeCreateInfoEXT mutableInfo;
    mutableInfo.setMutableDescriptorTypeLists(mutableList);

    vk::DescriptorSetLayoutBindingFlagsCreateInfo extended_info;
    constexpr vk::DescriptorBindingFlags bindlessFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
    std::vector<vk::DescriptorBindingFlags> binding_flags(descriptorLayoutInfo.bindingCount, bindlessFlags);
    extended_info.setBindingFlags(binding_flags);
    extended_info.setPNext(&mutableInfo);

    descriptorLayoutInfo.setFlags(vk::DescriptorSetLayoutCreateFlagBits::eDescriptorBufferEXT);
    descriptorLayoutInfo.setPNext(&mutableInfo);

    vk::DescriptorSetLayoutSupport support;
    context.device.getDescriptorSetLayoutSupport(&descriptorLayoutInfo, &support);
    if (!support.supported)
        log::exit("Mutable DescriptorSetLayout not supported");
    return context.device.createDescriptorSetLayoutUnique(descriptorLayoutInfo);
}

vk::UniqueDescriptorSetLayout BindlessTable::buildConstantLayout(const VulkanContext& context)
{
    vk::DescriptorSetLayoutBinding binding;
    binding.binding = 0;
    binding.descriptorCount = 1;
    binding.descriptorType = vk::DescriptorType::eUniformBuffer;
    binding.stageFlags = vk::ShaderStageFlagBits::eAll;

    log::debug("ConstantLayout:");
    log::debug("set = 1, binding = {}, count = {:04}, type = {}", binding.binding, binding.descriptorCount,
               vk::to_string(binding.descriptorType));
    log::debug("======================================================");

    vk::DescriptorSetLayoutCreateInfo descriptorLayoutInfo;
    descriptorLayoutInfo.setBindings(binding);

    vk::DescriptorSetLayoutBindingFlagsCreateInfo extended_info;
    constexpr vk::DescriptorBindingFlags bindlessFlags = vk::DescriptorBindingFlagBits::ePartiallyBound;
    std::vector<vk::DescriptorBindingFlags> binding_flags(descriptorLayoutInfo.bindingCount, bindlessFlags);
    extended_info.setBindingFlags(binding_flags);

    descriptorLayoutInfo.setFlags(vk::DescriptorSetLayoutCreateFlagBits::eDescriptorBufferEXT);
    // descriptorLayoutInfo.setPNext(&extended_info);

    vk::DescriptorSetLayoutSupport support;
    context.device.getDescriptorSetLayoutSupport(&descriptorLayoutInfo, &support);
    if (!support.supported)
        log::exit("Constant DescriptorSetLayout not supported");
    return context.device.createDescriptorSetLayoutUnique(descriptorLayoutInfo);
}

void BindlessTable::setSampler(const SamplerPtr& sampler, uint32_t slot)
{
    auto* native = checked_cast<Sampler*>(sampler.get());
    constexpr auto type = vk::DescriptorType::eSampler;

    vk::DescriptorImageInfo imageInfo;
    imageInfo.setSampler(native->handle.get());

    auto* cpuHandle = static_cast<std::byte*>(m_bufferDescriptor.hostInfo.pMappedData);
    cpuHandle += m_samplerDescriptorOffset;
    cpuHandle += slot * getDescriptorSizeForType(type);

    const vk::DescriptorGetInfoEXT info(type, &imageInfo);
    m_context.device.getDescriptorEXT(info, getDescriptorSizeForType(type), cpuHandle);
}

bool BindlessTable::visitTexture(const TexturePtr& texture, uint32_t slot)
{
    auto* image = checked_cast<Texture*>(texture.get());

    auto* cpuHandle = static_cast<std::byte*>(m_bufferDescriptor.hostInfo.pMappedData);
    cpuHandle += m_mutableDescriptorOffset;
    cpuHandle += slot * m_mutableDescriptorSize;

    constexpr auto type = vk::DescriptorType::eSampledImage;

    vk::DescriptorImageInfo imageInfo;
    imageInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    imageInfo.setImageView(image->view());

    const vk::DescriptorGetInfoEXT info(type, &imageInfo);
    m_context.device.getDescriptorEXT(info, getDescriptorSizeForType(type), cpuHandle);

    return true;
}

bool BindlessTable::visitBuffer(const BufferPtr& buffer, uint32_t slot)
{
    const auto* buff = checked_cast<Buffer*>(buffer.get());

    auto* cpuHandle = static_cast<std::byte*>(m_bufferDescriptor.hostInfo.pMappedData);
    cpuHandle += m_mutableDescriptorOffset;
    cpuHandle += slot * m_mutableDescriptorSize;

    vk::DescriptorAddressInfoEXT addrInfo;
    addrInfo.address = buff->getGPUAddress();
    addrInfo.range = buff->sizeInBytes();
    addrInfo.format = buff->format;

    vk::DescriptorType type = vk::DescriptorType::eStorageBuffer;
    if (buff->info.usage & vk::BufferUsageFlagBits::eUniformBuffer)
        type = vk::DescriptorType::eUniformBuffer;
    if (buff->info.usage & vk::BufferUsageFlagBits::eStorageTexelBuffer)
        type = vk::DescriptorType::eStorageTexelBuffer;

    const vk::DescriptorGetInfoEXT info(type, &addrInfo);
    m_context.device.getDescriptorEXT(info, getDescriptorSizeForType(type), cpuHandle);

    return true;
}

uint64_t BindlessTable::createCBV(Buffer* buffer)
{
    const vk::DescriptorType type = vk::DescriptorType::eUniformBuffer;

    auto* cpuHandle = static_cast<std::byte*>(m_bufferDescriptor.hostInfo.pMappedData);
    cpuHandle += getOffsetFromIndex(m_cbvCount);

    vk::DescriptorAddressInfoEXT addrInfo;
    addrInfo.address = buffer->getGPUAddress();
    addrInfo.range = buffer->sizeInBytes();
    addrInfo.format = buffer->format;

    const vk::DescriptorGetInfoEXT info(type, &addrInfo);
    m_context.device.getDescriptorEXT(info, getDescriptorSizeForType(type), cpuHandle);

    return m_cbvCount++;
}

vk::DeviceAddress BindlessTable::bufferDescriptorGPUAddress() const
{
    return m_bufferDescriptor.getGPUAddress();
}

uint64_t BindlessTable::getOffsetFromIndex(uint32_t index) const
{
    uint64_t cpuHandle = align(20000ull, 64ull);
    cpuHandle += index * m_mutableDescriptorSize;
    uint64_t test = align(cpuHandle, 64ull);
    return test;
}

BindlessTablePtr Device::createBindlessTable(uint32_t count)
{
    return std::make_shared<BindlessTable>(m_context, count);
}
} // namespace ler::rhi::vulkan