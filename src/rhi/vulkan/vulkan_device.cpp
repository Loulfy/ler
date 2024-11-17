//
// Created by loulfy on 01/12/2023.
//

#define VMA_IMPLEMENTATION
#include "rhi/vulkan.hpp"
#include "log/log.hpp"

#include <vulkan/vulkan_beta.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace ler::rhi::vulkan
{
    DevicePtr CreateDevice(const DeviceConfig& config)
    {
        return std::make_shared<Device>(config);
    }

    static bool gpuFilter(const vk::PhysicalDevice& phyDev)
    {
#ifdef PLATFORM_MACOS
        return phyDev.getProperties().deviceType == vk::PhysicalDeviceType::eIntegratedGpu;
#else
        return phyDev.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
#endif
    }

    Device::~Device()
    {
        m_storage.reset();
        vmaDestroyAllocator(m_context.allocator);
    }

    using PropertiesChain = vk::StructureChain<vk::PhysicalDeviceProperties2,
                vk::PhysicalDeviceSubgroupProperties,
                vk::PhysicalDeviceExternalMemoryHostPropertiesEXT>;

    using FeaturesChain = vk::StructureChain<vk::PhysicalDeviceFeatures2,
                vk::PhysicalDeviceVulkan11Features,
                vk::PhysicalDeviceVulkan12Features,
                vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceSynchronization2Features,
                vk::PhysicalDeviceDynamicRenderingFeatures,
                vk::PhysicalDeviceMutableDescriptorTypeFeaturesEXT,
                vk::PhysicalDeviceRayQueryFeaturesKHR,
                vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
                vk::PhysicalDeviceAccelerationStructureFeaturesKHR>;

    using InfoChain = vk::StructureChain<vk::DeviceCreateInfo,
                vk::PhysicalDeviceVulkan11Features,
                vk::PhysicalDeviceVulkan12Features,
                vk::PhysicalDeviceVulkan13Features,
                vk::PhysicalDeviceSynchronization2Features,
                vk::PhysicalDeviceDynamicRenderingFeatures,
                vk::PhysicalDeviceMutableDescriptorTypeFeaturesEXT,
                vk::PhysicalDeviceRayQueryFeaturesKHR,
                vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
                vk::PhysicalDeviceAccelerationStructureFeaturesKHR>;

    VulkanFeatures getSupportedFeatures(const std::set<std::string>& supportedExtensionSet, const PropertiesChain& p, const FeaturesChain& f)
    {
        VulkanFeatures features;
        features.apiVersion = p.get<vk::PhysicalDeviceProperties2>().properties.apiVersion;
        features.drawIndirectCount = f.get<vk::PhysicalDeviceVulkan12Features>().drawIndirectCount;

        if(supportedExtensionSet.contains(VK_KHR_RAY_QUERY_EXTENSION_NAME))
            features.rayTracing = true;
        if(supportedExtensionSet.contains(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME))
            features.hostBuffer = true;
        if(supportedExtensionSet.contains(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME))
            features.memoryPriority = true;
        if(supportedExtensionSet.contains(VK_KHR_PIPELINE_BINARY_EXTENSION_NAME))
            features.binaryPipeline = true;
        if(supportedExtensionSet.contains(VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME))
            features.mutableDescriptor = true;

        return features;
    }

    void populateRequiredDeviceExtensions(const VulkanFeatures& features, std::vector<const char*>& deviceExtensions)
    {
        // Enable Dynamic Rendering and Barrier2 for Vulkan 1.2
        if(VK_VERSION_MINOR(features.apiVersion) == 2)
        {
            deviceExtensions.emplace_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
            deviceExtensions.emplace_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        }

        // Enable RayTracing
        if(features.rayTracing)
        {
            deviceExtensions.emplace_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            deviceExtensions.emplace_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            deviceExtensions.emplace_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            deviceExtensions.emplace_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        }

        // Enable Pipeline library
        if(features.binaryPipeline)
        {
            deviceExtensions.emplace_back(VK_KHR_MAINTENANCE_5_EXTENSION_NAME);
            deviceExtensions.emplace_back(VK_KHR_PIPELINE_BINARY_EXTENSION_NAME);
        }

        // Enable VMA priority
        if(features.memoryPriority)
            deviceExtensions.emplace_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);

        // Malloc staging buffer
        if(features.hostBuffer)
            deviceExtensions.emplace_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);

        // Enable Mutable Descriptor
        if(features.mutableDescriptor)
            deviceExtensions.emplace_back(VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME);
    }

    void printVulkanFeatures(const VulkanFeatures& features, const PropertiesChain& p, const FeaturesChain& f)
    {
        uint32_t major = VK_VERSION_MAJOR(features.apiVersion);
        uint32_t minor = VK_VERSION_MINOR(features.apiVersion);
        uint32_t patch = VK_VERSION_PATCH(features.apiVersion);

        log::info("API: {}.{}.{}", major, minor, patch);
        auto subgroupProps = p.get<vk::PhysicalDeviceSubgroupProperties>();
        auto memoryProps = p.get<vk::PhysicalDeviceExternalMemoryHostPropertiesEXT>();
        log::info("RayTracing: {}", features.rayTracing);
        log::info("HostBuffer: {}", features.hostBuffer);
        log::info("MutableDescriptor: {}", features.mutableDescriptor);
        log::info("DrawIndirectCount: {}", features.drawIndirectCount);
        log::info("SubgroupSize: {}", subgroupProps.subgroupSize);
        log::info("Subgroup Support: {}", vk::to_string(subgroupProps.supportedOperations));
        log::info("MinImportedHostPointerAlignment: {}", memoryProps.minImportedHostPointerAlignment);
    }

    void unlinkUnsupportedFeatures(InfoChain& createInfoChain, const VulkanFeatures& features)
    {
        const uint32_t minor = VK_VERSION_MINOR(features.apiVersion);

        if(minor == 2)
            createInfoChain.unlink<vk::PhysicalDeviceVulkan13Features>();
        if(minor == 3)
        {
            createInfoChain.unlink<vk::PhysicalDeviceSynchronization2Features>();
            createInfoChain.unlink<vk::PhysicalDeviceDynamicRenderingFeatures>();
        }

        if(!features.mutableDescriptor)
            createInfoChain.unlink<vk::PhysicalDeviceMutableDescriptorTypeFeaturesEXT>();
        if(!features.rayTracing)
        {
            createInfoChain.unlink<vk::PhysicalDeviceRayQueryFeaturesKHR>();
            createInfoChain.unlink<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
            createInfoChain.unlink<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
        }
    }

    Device::Device(const DeviceConfig& config)
    {
        static const vk::DynamicLoader dl;
        const auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        vk::ResultValueType<uint32_t>::type sdkVersion = vk::enumerateInstanceVersion();
        uint32_t major = VK_VERSION_MAJOR(sdkVersion);
        uint32_t minor = VK_VERSION_MINOR(sdkVersion);
        uint32_t patch = VK_VERSION_PATCH(sdkVersion);

        log::info("Vulkan SDK {}.{}.{}", major, minor, patch);

        std::vector<const char*> extensions;
        extensions.reserve(10);
        if(config.debug)
        {
            extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            extensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
            extensions.emplace_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        }

        extensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        extensions.insert(extensions.end(), config.extensions.begin(), config.extensions.end());

        std::initializer_list<const char*> layers = {
                "VK_LAYER_KHRONOS_validation"
        };

        std::vector<const char*> deviceExtensions = {
                VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
                // VULKAN MEMORY ALLOCATOR
                VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
                VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
                VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
                VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,
                // SWAP CHAIN
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

#ifdef PLATFORM_MACOS
        extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        deviceExtensions.emplace_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        // Create instance
        vk::ApplicationInfo appInfo;
        appInfo.setApiVersion(VK_API_VERSION_1_3);
        appInfo.setPEngineName("LER");

        vk::InstanceCreateInfo instInfo;
        instInfo.setPApplicationInfo(&appInfo);
        if(config.debug)
            instInfo.setPEnabledLayerNames(layers);
        instInfo.setPEnabledExtensionNames(extensions);
#ifdef PLATFORM_MACOS
        instInfo.setFlags(vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR);
#endif
        m_instance = vk::createInstanceUnique(instInfo);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance.get());

        // Pick First GPU
        auto physicalDeviceList = m_instance->enumeratePhysicalDevices();
        auto it = std::ranges::find_if(physicalDeviceList, gpuFilter);
        if(it == physicalDeviceList.end())
            log::exit("No discrete GPU available");
        m_physicalDevice = *it;
        log::info("GPU: {}", m_physicalDevice.getProperties().deviceName.data());

        std::set<std::string> supportedExtensionSet;
        for (const auto& devExt: m_physicalDevice.enumerateDeviceExtensionProperties())
            supportedExtensionSet.insert(devExt.extensionName);

        for(const char* devExt : deviceExtensions)
        {
            if(!supportedExtensionSet.contains(devExt))
            {
                std::string fatal("Mandatory Device Extension Not Supported: ");
                fatal+= devExt;
                log::exit(fatal);
            }
        }

        // Device Properties
        PropertiesChain pp;
        m_physicalDevice.getProperties2(&pp.get<vk::PhysicalDeviceProperties2>());

        // Device Features
        FeaturesChain ff;
        m_physicalDevice.getFeatures2(&ff.get<vk::PhysicalDeviceFeatures2>());
        const vk::PhysicalDeviceFeatures& features = ff.get<vk::PhysicalDeviceFeatures2>().features;

        const VulkanFeatures vulkanFeatures = getSupportedFeatures(supportedExtensionSet, pp, ff);
        populateRequiredDeviceExtensions(vulkanFeatures, deviceExtensions);
        printVulkanFeatures(vulkanFeatures, pp, ff);

        // Find Graphics Queue
        const auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();
        const auto family = std::ranges::find_if(queueFamilies, [](const vk::QueueFamilyProperties& queueFamily) {
            return queueFamily.queueCount > 0 && queueFamily.queueFlags & vk::QueueFlagBits::eGraphics;
        });

        m_graphicsQueueFamily = std::distance(queueFamilies.begin(), family);

        // Find Transfer Queue (for parallel command)
        for(size_t i = 0; i < queueFamilies.size(); ++i)
        {
            if(queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & vk::QueueFlagBits::eTransfer && m_graphicsQueueFamily != i)
            {
                m_transferQueueFamily = i;
                break;
            }
        }

        if(m_transferQueueFamily == UINT32_MAX)
            throw std::runtime_error("No transfer queue available");

        // Create queues
        float queuePriority = 1.0f;
        std::initializer_list<vk::DeviceQueueCreateInfo> queueCreateInfos = {
                { {}, m_graphicsQueueFamily, 1, &queuePriority },
                { {}, m_transferQueueFamily, 1, &queuePriority },
        };

        for(auto& q : queueCreateInfos)
            log::info("Queue Family {}: {}", q.queueFamilyIndex, vk::to_string(queueFamilies[q.queueFamilyIndex].queueFlags));

        // Create Device
        vk::DeviceCreateInfo deviceInfo;
        deviceInfo.setQueueCreateInfos(queueCreateInfos);
        deviceInfo.setPEnabledExtensionNames(deviceExtensions);
        if(config.debug)
            deviceInfo.setPEnabledLayerNames(layers);
        deviceInfo.setPEnabledFeatures(&features);

        vk::PhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableFeature;
        mutableFeature.setMutableDescriptorType(true);

        vk::PhysicalDevicePipelineBinaryFeaturesKHR pipelineFeature;
        pipelineFeature.setPipelineBinaries(true);

        vk::PhysicalDeviceSynchronization2Features sync2Features;
        sync2Features.setSynchronization2(true);

        vk::PhysicalDeviceDynamicRenderingFeatures dynRenderFeature;
        dynRenderFeature.setDynamicRendering(true);

        vk::PhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures;
        rayQueryFeatures.setRayQuery(true);

        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures;
        rayTracingPipelineFeatures.setRayTracingPipeline(true);

        vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures;
        accelerationStructureFeatures.setAccelerationStructure(true);

        InfoChain createInfoChain(
                deviceInfo,
                ff.get<vk::PhysicalDeviceVulkan11Features>(),
                ff.get<vk::PhysicalDeviceVulkan12Features>(),
                ff.get<vk::PhysicalDeviceVulkan13Features>(),
                sync2Features,
                dynRenderFeature,
                mutableFeature,
                rayQueryFeatures,
                rayTracingPipelineFeatures,
                accelerationStructureFeatures);

        unlinkUnsupportedFeatures(createInfoChain, vulkanFeatures);

        m_device = m_physicalDevice.createDeviceUnique(createInfoChain.get<vk::DeviceCreateInfo>());
        VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device.get());

        m_pipelineCache = m_device->createPipelineCacheUnique({});

        // Create Memory Allocator
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion = vulkanFeatures.apiVersion;
        allocatorCreateInfo.device = m_device.get();
        allocatorCreateInfo.instance = m_instance.get();
        allocatorCreateInfo.physicalDevice = m_physicalDevice;
        allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
        allocatorCreateInfo.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;

        VmaAllocator allocator;
        vmaCreateAllocator(&allocatorCreateInfo, &allocator);

        m_context.device = m_device.get();
        m_context.instance = m_instance.get();
        m_context.physicalDevice = m_physicalDevice;
        m_context.graphicsQueueFamily = m_graphicsQueueFamily;
        m_context.transferQueueFamily = m_transferQueueFamily;
        m_context.pipelineCache = m_pipelineCache.get();
        m_context.allocator = allocator;
        m_context.debug = config.debug;

        m_bindlessLayout = BindlessTable::buildBindlessLayout(m_context, 128, false);
        m_context.bindlessLayout = m_bindlessLayout.get();

        m_context.hostBuffer = config.hostBuffer;
        m_context.hostPointerAlignment = pp.get<vk::PhysicalDeviceExternalMemoryHostPropertiesEXT>().minImportedHostPointerAlignment;

        m_queues[int(QueueType::Graphics)] = std::make_unique<Queue>(m_context, QueueType::Graphics);
        m_queues[int(QueueType::Transfer)] = std::make_unique<Queue>(m_context, QueueType::Transfer);

        m_threadPool = std::make_shared<coro::thread_pool>(coro::thread_pool::options{ .thread_count = 8 });

        m_storage = std::make_shared<Storage>(this, m_threadPool);
        m_library = std::make_shared<PSOLibrary>(this);
    }

    void Buffer::uploadFromMemory(const void* src, uint32_t byteSize) const
    {
        if (staging() && sizeBytes() >= byteSize)
            std::memcpy(hostInfo.pMappedData, src, byteSize);
        else
            log::error("Failed to upload to buffer");
    }

    void Buffer::getUint(uint32_t* ptr) const
    {
        if (staging())
        {
            auto data = (uint32_t*) hostInfo.pMappedData;
            *ptr = *data;
        }
        else
            log::error("Failed to upload to buffer");
    }

    static void aligned_free(void* pMemory)
    {
        #ifdef _WIN32
            _aligned_free(pMemory);
        #else
            free(pMemory);
        #endif
    }

    static void* aligned_malloc(size_t size, size_t align)
    {
        #ifdef _WIN32
            return _aligned_malloc(size, align);
        #else
            return aligned_alloc(align, size);
        #endif
    }

    Buffer::~Buffer()
    {
        if(allocation)
            vmaDestroyBuffer(m_context.allocator, static_cast<VkBuffer>(handle), allocation);
        else
        {
            m_context.device.unmapMemory(hostInfo.deviceMemory);
            m_context.device.destroyBuffer(handle);
            m_context.device.freeMemory(hostInfo.deviceMemory);
            aligned_free(hostInfo.pMappedData);
            hostInfo.pMappedData = nullptr;
        }
    }

    static uint32_t findMemoryType(uint32_t memoryTypeBits, const vk::PhysicalDeviceMemoryProperties& memoryProperties)
    {
        uint32_t idx;
        for (idx = 0; idx < memoryProperties.memoryTypeCount; ++idx)
        {
            if (memoryTypeBits & (1U << idx))
                break;
        }
        return idx;
    }

    BufferPtr Device::createHostBuffer(uint32_t byteSize)
    {
        // Fallback to standard staging buffer
        if(m_context.hostBuffer == false)
            return createBuffer(byteSize, true);

        // Host Buffer: staging buffer allocated with malloc (external memory).
        byteSize = align(byteSize, m_context.hostPointerAlignment);

        auto buffer = std::make_shared<Buffer>(m_context);
        const vk::BufferUsageFlags usageFlags =
            vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
        constexpr vk::ExternalMemoryBufferCreateInfo extMemBuffInfo{vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT};
        buffer->info = vk::BufferCreateInfo();
        buffer->info.setPNext(&extMemBuffInfo);
        buffer->info.setSize(byteSize);
        buffer->info.setUsage(usageFlags);
        buffer->info.setSharingMode(vk::SharingMode::eExclusive);

        if (m_context.device.createBuffer(&buffer->info, nullptr, &buffer->handle) != vk::Result::eSuccess)
            throw std::runtime_error("failed to create buffer!");

        vk::MemoryRequirements memRequirements;
        m_context.device.getBufferMemoryRequirements(buffer->handle, &memRequirements);

        void* hostPtr = aligned_malloc(byteSize, m_context.hostPointerAlignment);
        const vk::MemoryHostPointerPropertiesEXT hostProps = m_context.device.getMemoryHostPointerPropertiesEXT(
            vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT, hostPtr);

        const uint32_t memoryTypeBits = memRequirements.memoryTypeBits & hostProps.memoryTypeBits;

        vk::ImportMemoryHostPointerInfoEXT importMemInfo;
        importMemInfo.setPHostPointer(hostPtr);
        importMemInfo.setHandleType(vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT);

        vk::MemoryAllocateInfo allocInfo;
        allocInfo.pNext = &importMemInfo;
        allocInfo.allocationSize = memRequirements.size;
        const auto& memoryProperties = m_context.physicalDevice.getMemoryProperties();
        allocInfo.memoryTypeIndex = findMemoryType(memoryTypeBits, memoryProperties);

        buffer->hostInfo.deviceMemory = m_context.device.allocateMemory(allocInfo);
        m_context.device.bindBufferMemory(buffer->handle, buffer->hostInfo.deviceMemory, 0);
        buffer->hostInfo.pMappedData = m_context.device.mapMemory(buffer->hostInfo.deviceMemory, 0, byteSize);

        return buffer;
    }

    BufferPtr Device::createBuffer(uint32_t byteSize, bool staging)
    {
        auto buffer = std::make_shared<Buffer>(m_context);
        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
        usageFlags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        //usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        if(staging)
            usageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;
        buffer->info = vk::BufferCreateInfo();
        buffer->info.setSize(byteSize);
        buffer->info.setUsage(usageFlags);
        buffer->info.setSharingMode(vk::SharingMode::eExclusive);

        buffer->allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        buffer->allocInfo.flags = staging ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                            VMA_ALLOCATION_CREATE_MAPPED_BIT
                                          : VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        vmaCreateBuffer(m_context.allocator, reinterpret_cast<VkBufferCreateInfo*>(&buffer->info), &buffer->allocInfo,
                        reinterpret_cast<VkBuffer*>(&buffer->handle), &buffer->allocation, &buffer->hostInfo);

        return buffer;
    }

    BufferPtr Device::createBuffer(const BufferDesc& desc)
    {
        auto buffer = std::make_shared<Buffer>(m_context);
        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
        if(desc.isVertexBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
        if(desc.isIndexBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
        if(desc.isConstantBuffer)
            usageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;
        else
            usageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
        if(desc.isDrawIndirectArgs)
            usageFlags |= vk::BufferUsageFlagBits::eIndirectBuffer;
        if(desc.isAccelStructBuildInput)
        {
            usageFlags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
            usageFlags |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        }
        buffer->info = vk::BufferCreateInfo();
        buffer->info.setSize(desc.byteSize);
        buffer->info.setUsage(usageFlags);
        buffer->info.setSharingMode(vk::SharingMode::eExclusive);

        log::debug("Create Buffer {} -> {}", desc.debugName, vk::to_string(usageFlags));
        buffer->allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        buffer->allocInfo.flags = desc.isStaging ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                            VMA_ALLOCATION_CREATE_MAPPED_BIT
                                          : VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if(desc.alignment == 0)
        {
            vmaCreateBuffer(m_context.allocator, reinterpret_cast<VkBufferCreateInfo*>(&buffer->info), &buffer->allocInfo,
                            reinterpret_cast<VkBuffer*>(&buffer->handle), &buffer->allocation, &buffer->hostInfo);
        }
        else
        {
            vmaCreateBufferWithAlignment(m_context.allocator, reinterpret_cast<VkBufferCreateInfo*>(&buffer->info), &buffer->allocInfo, desc.alignment,
                                         reinterpret_cast<VkBuffer*>(&buffer->handle), &buffer->allocation, &buffer->hostInfo);
        }

        return buffer;
    }

    TexturePtr Device::createTexture(const TextureDesc& desc)
    {
        auto texture = std::make_shared<Texture>(m_context);
        populateTexture(texture, desc);
        return texture;
    }

    void Device::waitIdle()
    {
        m_device->waitIdle();
    }
}