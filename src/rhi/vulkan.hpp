//
// Created by loulfy on 01/12/2023.
//

#pragma once

#include "rhi.hpp"
#include "queue.hpp"
#include "storage.hpp"
#include "sys/thread.hpp"
#include "sys/ioring.hpp"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <mutex>
#include <queue>
#include <set>

namespace ler
{
    // Boost hash_combine
    template<class T>
    void hash_combine(size_t& seed, const T& v)
    {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
}

namespace std
{
    template<> struct hash<vk::ImageSubresourceRange>
    {
        std::size_t operator()(vk::ImageSubresourceRange const& s) const noexcept
        {
            size_t hash = 0;
            ler::hash_combine(hash, s.baseMipLevel);
            ler::hash_combine(hash, s.levelCount);
            ler::hash_combine(hash, s.baseArrayLayer);
            ler::hash_combine(hash, s.layerCount);
            return hash;
        }
    };
}

namespace ler::rhi::vulkan
{
    struct VulkanContext
    {
        vk::Instance instance;
        vk::PhysicalDevice physicalDevice;
        vk::Device device;
        uint32_t graphicsQueueFamily = UINT32_MAX;
        uint32_t transferQueueFamily = UINT32_MAX;
        VmaAllocator allocator = nullptr;
        vk::PipelineCache pipelineCache;
        uint32_t hostPointerAlignment = 4096u;
        bool hostBuffer = true;
        bool debug = false;
    };

    struct Buffer final : public IBuffer
    {
        vk::Buffer handle;
        vk::BufferCreateInfo info;
        VmaAllocation allocation = nullptr;
        VmaAllocationCreateInfo allocInfo = {};
        VmaAllocationInfo hostInfo = {};

        ~Buffer() override;
        explicit Buffer(const VulkanContext& context) : m_context(context) { }
        [[nodiscard]] uint32_t sizeBytes() const override { return info.size; }
        [[nodiscard]] bool staging() const override { return allocInfo.flags & VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT; }
        void uploadFromMemory(const void* src, uint32_t byteSize) const override;

    private:

        const VulkanContext& m_context;
    };

    struct Texture final : public ITexture
    {
        vk::Image handle;
        std::string name;
        vk::ImageCreateInfo info;
        VmaAllocation allocation = nullptr;
        VmaAllocationCreateInfo allocInfo = {};

        ~Texture() override { if(allocation) vmaDestroyImage(m_context.allocator, static_cast<VkImage>(handle), allocation); }
        explicit Texture(const VulkanContext& context) : m_context(context) { }
        [[nodiscard]] vk::ImageView view(vk::ImageSubresourceRange subresource = DefaultSub);
        //[[nodiscard]] vk::Extent2D extent() const;
        void setName(const std::string& debugName);

        [[nodiscard]] Extent extent() const override { return {info.extent.width, info.extent.height}; }

        static constexpr vk::ImageSubresourceRange DefaultSub = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

    private:

        const VulkanContext& m_context;
        std::unordered_map<vk::ImageSubresourceRange, vk::UniqueImageView> m_views;
    };

    class Sampler final : public ISampler
    {
    public:

        vk::UniqueSampler handle;
    };

    struct DescriptorSetLayoutData
    {
        uint32_t set_number = 0;
        VkDescriptorSetLayoutCreateInfo create_info;
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
    };

    struct Shader
    {
        std::string entryPoint = "main";
        vk::UniqueShaderModule shaderModule;
        vk::ShaderStageFlagBits stageFlagBits = {};
        vk::PipelineVertexInputStateCreateInfo pvi;
        std::vector<vk::PushConstantRange> pushConstants;
        std::map<uint32_t, DescriptorSetLayoutData> descriptorMap;
        std::vector<vk::VertexInputBindingDescription> bindingDesc;
        std::vector<vk::VertexInputAttributeDescription> attributeDesc;
    };

    using ShaderPtr = std::shared_ptr<Shader>;

    struct DescriptorAllocator
    {
        std::vector<vk::DescriptorSetLayoutBinding> layoutBinding;
        vk::UniqueDescriptorSetLayout layout;
        vk::UniqueDescriptorPool pool;
    };

    class BasePipeline : public IPipeline
    {
    public:

        explicit BasePipeline(const VulkanContext& context) : m_context(context) { }
        void reflectPipelineLayout(vk::Device device, const std::span<ShaderPtr>& shaders);
        void createDescriptorSet(uint32_t set) override;
        void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, TexturePtr& texture) override;
        void updateStorage(uint32_t descriptor, uint32_t binding, BufferPtr& buffer, uint64_t byteSize) override;
        void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, const std::span<TexturePtr>& textures) override;

        std::optional<vk::DescriptorType> findBindingType(uint32_t set, uint32_t binding);
        [[nodiscard]] vk::DescriptorSet getDescriptorSet(uint32_t id) const { return m_descriptors[id]; }

        vk::UniquePipeline handle;
        vk::UniquePipelineLayout pipelineLayout;
        vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics;
        std::unordered_map<uint32_t,DescriptorAllocator> descriptorAllocMap;
        std::unordered_map<VkDescriptorSet, uint32_t> descriptorPoolMap;

    private:

        const VulkanContext& m_context;
        std::vector<vk::DescriptorSet> m_descriptors;
    };

    using PipelinePtr = std::shared_ptr<BasePipeline>;

    class GraphicsPipeline final : public BasePipeline
    {
    public:

        explicit GraphicsPipeline(const VulkanContext& context) : BasePipeline(context) { }
    };

    class ComputePipeline final : public BasePipeline
    {
    public:

        explicit ComputePipeline(const VulkanContext& context) : BasePipeline(context) { }
    };

    class Command final : public ICommand
    {
    public:
        // the command buffer itself
        vk::CommandBuffer cmdBuf = vk::CommandBuffer();
        vk::CommandPool cmdPool = vk::CommandPool();

        //uint64_t submissionID = 0;
        //QueueType queueType = QueueType::Graphics;
        //std::vector<std::shared_ptr<IResource>> referencedResources;

        ~Command() override;
        explicit Command(const VulkanContext& context) : m_context(context){ }

        void reset() override;
        void bindPipeline(const rhi::PipelinePtr& pipeline, uint32_t descriptorHandle) const override;
        void drawIndexed(uint32_t vertexCount) const override;
        void drawIndexedInstanced(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex, uint32_t firstId) const override;
        void drawIndirectIndexed(const rhi::PipelinePtr& pipeline, const BufferPtr& commands, const BufferPtr& count, uint32_t maxDrawCount, uint32_t stride) const override;
        void dispatch(uint32_t x, uint32_t y, uint32_t z) const override;
        void endRendering() const override;
        void beginRendering(const RenderingInfo& renderingInfo) const override;
        void beginRendering(const rhi::PipelinePtr& pipeline, TexturePtr& backBuffer) override;
        void addImageBarrier(const TexturePtr& texture, ResourceState new_state) const override;
        void addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const override;
        void clearColorImage(const TexturePtr& texture, const std::array<float,4>& color) const override;
        void copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub, const unsigned char* pSrcData) const override;
        void copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t byteSize, uint64_t dstOffset) override;
        void fillBuffer(const BufferPtr& dst, uint32_t value) const override;
        void bindIndexBuffer(const BufferPtr& indexBuffer) override;
        void bindVertexBuffers(uint32_t slot, const BufferPtr& indexBuffer) override;
        void beginDebugEvent(const std::string& label, const std::array<float, 4>& color) const override;
        void endDebugEvent() const override;

    private:

        const VulkanContext& m_context;
    };

    class Queue final : public rhi::Queue
    {
    public:

        friend class Device;
        using CommandPtr = std::shared_ptr<Command>;
        Queue(const VulkanContext& context, QueueType queueID);

        uint64_t updateLastFinishedID() override;

        // submission
        uint64_t submit(const std::span<CommandPtr>& ppCmd);
        void submitAndWait(const rhi::CommandPtr& command);

        vk::UniqueSemaphore trackingSemaphore;

    private:

        rhi::CommandPtr createCommandBuffer() override;

        const VulkanContext& m_context;

        vk::Queue m_queue = vk::Queue();
        uint32_t m_queueFamilyIndex = UINT32_MAX;

        std::vector<vk::Semaphore> m_waitSemaphores;
        std::vector<uint64_t> m_waitSemaphoreValues;
        std::vector<vk::Semaphore> m_signalSemaphores;
        std::vector<uint64_t> m_signalSemaphoreValues;
    };

    class Device;
    struct SwapChain final : public ISwapChain
    {
        explicit SwapChain(const VulkanContext& context) : m_context(context) { }
        uint32_t present(const RenderPass& renderPass) override;
        void resize(uint32_t width, uint32_t height) override;
        [[nodiscard]] Extent extent() const override;
        [[nodiscard]] Format format() const override;

        void createNativeSync();

        Device* device = nullptr;
        vk::UniqueSurfaceKHR surface;
        vk::SwapchainCreateInfoKHR createInfo;
        uint32_t swapChainIndex = 0;
        uint32_t currentFrame = 0;

        static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes, bool vSync);
        static vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);
        static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);

    private:

        vk::Queue m_queue = nullptr;
        const VulkanContext& m_context;
        vk::UniqueSwapchainKHR m_handle;
        std::vector<TexturePtr> m_images;
        std::array<CommandPtr,FrameCount> m_command;
        std::array<vk::UniqueFence,FrameCount> m_fences;
        std::array<vk::CommandBuffer,FrameCount> m_rawCommand;
        std::array<vk::UniqueSemaphore,FrameCount> m_presentSemaphore;
        std::array<vk::UniqueSemaphore,FrameCount> m_acquireSemaphore;
    };

    struct ReadOnlyFile final : public IReadOnlyFile
    {
        explicit ReadOnlyFile(const fs::path& path) : handle(path) {}
        std::string getFilename() override { return handle.getPath(); }
        uint32_t sizeBytes() override { return handle.m_size; }

        sys::ReadOnlyFile handle;
    };

    class Storage final : public IStorage
    {
    public:

        ~Storage() override;
        Storage(const VulkanContext& context, Device* device, std::shared_ptr<coro::thread_pool>& tp);
        void update() override;
        ReadOnlyFilePtr openFile(const fs::path& path) override;
        std::vector<ReadOnlyFilePtr> openFiles(const fs::path& path, const fs::path& ext) override;

        void requestLoadTexture(coro::latch& latch, TexturePoolPtr& texturePool, const std::span<ReadOnlyFilePtr>& files) override;
        void requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer, uint32_t fileLength, uint32_t fileOffset) override;

        //private:

        img::ITexture* factoryTexture(const fs::path& filename, std::byte* metadata);
        //BufferPtr getStaging(uint32_t wantedSize);
        const BufferPtr& getStaging(int index) const { return m_stagings[index]; }
        void putStaging(const BufferPtr& staging);
        void releaseStaging(uint32_t index);
        int acquireStaging();

        static constexpr uint32_t kStagingSize = sys::C128Mio;
        Device* m_device = nullptr;
        const VulkanContext& m_context;
        sys::ThreadPool m_threadPool;

        using Allocator = std::pmr::polymorphic_allocator<>;
        std::unique_ptr<std::byte[]> m_buffer = std::make_unique<std::byte[]>(sys::C04Mio);
        std::pmr::monotonic_buffer_resource m_memory;

        std::vector<BufferPtr> m_stagings = {};
        mutable std::mutex m_staging_mutex = {};
        sys::Bitset m_staging_bitset;
        std::binary_semaphore m_semaphore;

        using task_container = coro::task_container<coro::thread_pool>;
        std::unique_ptr<task_container> m_scheduler;
        sys::IoService m_ios;

      private:

        coro::task<> makeSingleTextureTask(coro::latch& latch, TexturePoolPtr texturePool, ReadOnlyFilePtr file);
        coro::task<> makeMultiTextureTask(coro::latch& latch, TexturePoolPtr texturePool, std::vector<ReadOnlyFilePtr> files);
        coro::task<> makeBufferTask(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer, uint32_t fileLength, uint32_t fileOffset);
    };

    class Device final : public IDevice
    {
    public:

        explicit Device(const DeviceConfig& config);
        ~Device() override;

        // Buffer
        BufferPtr createBuffer(uint32_t byteSize, bool staging) override;
        BufferPtr createBuffer(const BufferDesc& desc) override;
        BufferPtr createHostBuffer(uint32_t byteSize) override;

        // Texture
        TexturePtr createTexture(const TextureDesc& desc) override;
        SamplerPtr createSampler(const SamplerDesc& desc) override;
        SwapChainPtr createSwapChain(GLFWwindow* window) override;

        // Pipeline
        [[nodiscard]] ShaderPtr createShader(const ShaderModule& shaderModule) const;
        [[nodiscard]] rhi::PipelinePtr createGraphicsPipeline(const std::vector<ShaderModule>& shaderModules, const PipelineDesc& desc) override;
        [[nodiscard]] rhi::PipelinePtr createComputePipeline(const ShaderModule& shaderModule) override;

        // Execution
        void waitIdle() override;
        [[nodiscard]] CommandPtr createCommand(QueueType type) override;
        void submitCommand(CommandPtr& command) override;
        void submitOneShot(const CommandPtr& command) override;
        void runGarbageCollection() override;

        StoragePtr getStorage() override { return m_storage; }

        [[nodiscard]] GraphicsAPI getGraphicsAPI() const override { return GraphicsAPI::VULKAN; }

        static vk::Format convertFormat(Format format);
        static Format reverseFormat(vk::Format format);
        static vk::SampleCountFlagBits pickImageSample(uint32_t samples);
        static vk::ImageAspectFlags guessImageAspectFlags(vk::Format format, bool stencil);

        [[nodiscard]] Queue* getGraphicsQueue() const { return m_queues[0].get(); }
        [[nodiscard]] Queue* getTransferQueue() const { return m_queues[2].get(); }

    private:

        static uint32_t formatSize(VkFormat format);
        void populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const;

        vk::UniqueInstance m_instance;
        vk::PhysicalDevice m_physicalDevice;
        uint32_t m_graphicsQueueFamily = UINT32_MAX;
        uint32_t m_transferQueueFamily = UINT32_MAX;
        vk::UniqueDevice m_device;
        vk::UniquePipelineCache m_pipelineCache;
        std::shared_ptr<coro::thread_pool> m_threadPool;
        VulkanContext m_context;
        std::array<std::unique_ptr<Queue>, uint32_t(QueueType::Count)> m_queues;
        std::shared_ptr<Storage> m_storage;
    };

    DevicePtr CreateDevice(const DeviceConfig& config);
}
