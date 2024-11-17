//
// Created by loulfy on 01/12/2023.
//

#pragma once

#include "rhi.hpp"
#include "queue.hpp"
#include "storage.hpp"
#include "bindless.hpp"
#include "sys/thread.hpp"
#include "sys/ioring.hpp"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <semaphore>
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
    struct VulkanFeatures
    {
        uint32_t apiVersion = VK_API_VERSION_1_3;
        bool hostBuffer = false;
        bool rayTracing = false;
        bool memoryPriority = false;
        bool binaryPipeline = false;
        bool mutableDescriptor = false;
        bool drawIndirectCount = false;
    };

    struct VulkanContext
    {
        vk::Instance instance;
        vk::PhysicalDevice physicalDevice;
        vk::Device device;
        uint32_t graphicsQueueFamily = UINT32_MAX;
        uint32_t transferQueueFamily = UINT32_MAX;
        VmaAllocator allocator = nullptr;
        vk::PipelineCache pipelineCache;
        vk::DescriptorSetLayout bindlessLayout;
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
        void getUint(uint32_t* ptr) const override;

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

    class BindlessTable : public CommonBindlessTable
    {
      public:
        BindlessTable(const VulkanContext& context, uint32_t count);
        void setSampler(const SamplerPtr& sampler, uint32_t slot) override;

        static vk::UniqueDescriptorSetLayout buildBindlessLayout(const VulkanContext& context, uint32_t count, bool useMutable);
        vk::DescriptorSet m_descriptor;

      private:
        const VulkanContext& m_context;
        vk::UniqueDescriptorPool m_pool;
        vk::UniqueDescriptorSetLayout m_layout;

        static constexpr std::initializer_list<vk::DescriptorType> kStandard = {
            vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eSampler,
            vk::DescriptorType::eUniformBuffer,
            vk::DescriptorType::eStorageBuffer,
        };

        static constexpr std::initializer_list<vk::DescriptorType> kMutable = {
            vk::DescriptorType::eMutableEXT,
            vk::DescriptorType::eSampler
        };

        bool visitTexture(const TexturePtr& texture, uint32_t slot) override;
        bool visitBuffer(const BufferPtr& buffer, uint32_t slot) override;
    };

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
        void bindPipeline(const rhi::PipelinePtr& pipeline, const BindlessTablePtr& table) const override;
        void pushConstant(const rhi::PipelinePtr& pipeline, ShaderType stage, const void* data, uint8_t size) const override;
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
        void resize(uint32_t width, uint32_t height, bool vsync) override;
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

    class Storage : public CommonStorage
    {
      public:
        Storage(Device* device, std::shared_ptr<coro::thread_pool>& tp);
        ReadOnlyFilePtr openFile(const fs::path& path) override;

      private:
        sys::IoService m_ios;

        coro::task<> makeSingleTextureTask(coro::latch& latch, BindlessTablePtr table, ReadOnlyFilePtr file) override;
        coro::task<> makeMultiTextureTask(coro::latch& latch, BindlessTablePtr table, std::vector<ReadOnlyFilePtr> files) override;
        coro::task<> makeBufferTask(coro::latch& latch, ReadOnlyFilePtr file, BufferPtr buffer, uint32_t fileLength, uint32_t fileOffset) override;
    };

    class PSOLibrary
    {
      public:
        explicit PSOLibrary(Device* device);

      private:
        Device* m_device = nullptr;
    };

    class ImGuiPass : public IRenderPass
    {
      public:

        ~ImGuiPass() override;
        void begin() override;
        void create(const DevicePtr& device, const SwapChainPtr& swapChain) override;
        void render(TexturePtr& backBuffer, CommandPtr& command) override;

      private:

        vk::UniqueDescriptorPool m_descriptorPool;
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
        SwapChainPtr createSwapChain(GLFWwindow* window, bool vsync) override;

        // Pipeline
        [[nodiscard]] BindlessTablePtr createBindlessTable(uint32_t size) override;
        [[nodiscard]] ShaderPtr createShader(const ShaderModule& shaderModule) const;
        [[nodiscard]] rhi::PipelinePtr createGraphicsPipeline(const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc) override;
        [[nodiscard]] rhi::PipelinePtr createComputePipeline(const ShaderModule& shaderModule) override;

        [[nodiscard]] rhi::PipelinePtr loadPipeline(const std::string& name, const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc) override;

        // Execution
        void waitIdle() override;
        [[nodiscard]] CommandPtr createCommand(QueueType type) override;
        void submitCommand(CommandPtr& command) override;
        void submitOneShot(const CommandPtr& command) override;
        void runGarbageCollection() override;

        StoragePtr getStorage() override { return m_storage; }

        [[nodiscard]] GraphicsAPI getGraphicsAPI() const override { return GraphicsAPI::VULKAN; }
        [[nodiscard]] const VulkanContext& getContext() const { return m_context; }

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
        vk::UniqueDescriptorSetLayout m_bindlessLayout;
        std::shared_ptr<coro::thread_pool> m_threadPool;
        VulkanContext m_context;
        std::array<std::unique_ptr<Queue>, uint32_t(QueueType::Count)> m_queues;
        std::shared_ptr<Storage> m_storage;
        std::shared_ptr<PSOLibrary> m_library;
    };

    DevicePtr CreateDevice(const DeviceConfig& config);
}
