//
// Created by loulfy on 21/11/2024.
//

#pragma once

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include "bindless.hpp"
#include "queue.hpp"
#include "rhi.hpp"
#include "storage.hpp"

namespace ler::rhi::metal
{
struct MetalContext
{
    MTL::Device* device = nullptr;
    MTL::CommandQueue* queue = nullptr;
};

struct Buffer final : public IBuffer
{
    MTL::Buffer* handle = nullptr;

    // clang-format off
    ~Buffer() override = default;
    explicit Buffer(const MetalContext& context) : m_context(context) {}
    [[nodiscard]] uint64_t sizeBytes() const override { return handle->length(); }
    [[nodiscard]] bool staging() const override { return handle->resourceOptions() == MTL::StorageModeShared; }
    void uploadFromMemory(const void* src, uint64_t byteSize) const override;
    void getUint(uint32_t* ptr) const override;
    // clang-format on

  private:
    const MetalContext& m_context;
};

struct Texture final : public ITexture
{
    MTL::Texture* handle = nullptr;

    // clang-format off
    ~Texture() override = default;
    explicit Texture(const MetalContext& context) : m_context(context) {}

    [[nodiscard]] Extent extent() const override { return {0, 0}; }
    // clang-format on

  private:
    const MetalContext& m_context;
};

class Command final : public ICommand
{
  public:
    // the command buffer itself
    MTL::CommandBuffer* cmdBuf = nullptr;

    // clang-format off
    ~Command() override = default;
    explicit Command(const MetalContext& context) : m_context(context) {}

    void reset() override;
    void bindPipeline(const rhi::PipelinePtr& pipeline, uint32_t descriptorHandle) const override {}
    void bindPipeline(const rhi::PipelinePtr& pipeline, const BindlessTablePtr& table) const override {}
    void pushConstant(const rhi::PipelinePtr& pipeline, ShaderType stage, const void* data, uint8_t size) const override {}
    void drawIndexed(uint32_t vertexCount) const override {}
    void drawIndexedInstanced(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex, uint32_t firstId) const override {}
    void drawIndirectIndexed(const rhi::PipelinePtr& pipeline, const BufferPtr& commands, const BufferPtr& count, uint32_t maxDrawCount, uint32_t stride) const override {}
    void dispatch(uint32_t x, uint32_t y, uint32_t z) const override {}
    void endRendering() const override {}
    void beginRendering(const RenderingInfo& renderingInfo) const override {}
    void beginRendering(const rhi::PipelinePtr& pipeline, TexturePtr& backBuffer) override {}
    void addImageBarrier(const TexturePtr& texture, ResourceState new_state) const override {}
    void addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const override {}
    void clearColorImage(const TexturePtr& texture, const std::array<float,4>& color) const override {}
    void copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub, const unsigned char* pSrcData) const override {}
    void copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t byteSize, uint64_t dstOffset) override {}
    void fillBuffer(const BufferPtr& dst, uint32_t value) const override {}
    void bindIndexBuffer(const BufferPtr& indexBuffer) override {}
    void bindVertexBuffers(uint32_t slot, const BufferPtr& indexBuffer) override {}
    void beginDebugEvent(const std::string& label, const std::array<float, 4>& color) const override {}
    void endDebugEvent() const override {}
    // clang-format on

  private:
    const MetalContext& m_context;
};

class Queue final : public rhi::Queue
{
  public:
    friend class Device;
    using CommandPtr = std::shared_ptr<Command>;
    Queue(const MetalContext& context, QueueType queueID);

    uint64_t updateLastFinishedID() override;

    // submission
    uint64_t submit(const std::span<CommandPtr>& ppCmd);
    void submitAndWait(const rhi::CommandPtr& command);

  private:
    rhi::CommandPtr createCommandBuffer() override;

    const MetalContext& m_context;
    MTL::CommandQueue* m_queue = nullptr;
    MTL::SharedEvent* m_eventTracking = nullptr;
};

class Device;
struct SwapChain final : public ISwapChain
{
    // clang-format off
    explicit SwapChain(const MetalContext& context) : m_context(context) { }
    uint32_t present(const RenderPass& renderPass) override;
    void resize(uint32_t width, uint32_t height, bool vsync) override;
    [[nodiscard]] Extent extent() const override;
    //[[nodiscard]] Format format() const override;

    void createNativeSync();

    Device* device = nullptr;
    NS::SharedPtr<CA::MetalLayer> layer;
    uint32_t swapChainIndex = 0;
    uint32_t currentFrame = 0;
    // clang-format on

    //static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes, bool vSync);
    //static vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);
    //static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);

private:

    uint32_t m_frameIndex = 0;
    NS::SharedPtr<MTL::SharedEvent> m_fence;
    dispatch_semaphore_t m_imageAcquireSemaphore = nil;
    std::array<uint64_t,FrameCount> m_fenceValues = {};
    std::array<TexturePtr,FrameCount> m_images;
    std::array<CommandPtr,FrameCount> m_command;
    const MetalContext& m_context;
};

class ImGuiPass : public IRenderPass
{
public:

    ~ImGuiPass() override;
    void begin(TexturePtr& backBuffer) override;
    void create(const DevicePtr& device, const SwapChainPtr& swapChain) override;
    void render(TexturePtr& backBuffer, CommandPtr& command) override;

private:

    MTL::RenderPassDescriptor* m_renderPassDescriptor;
};

class Device final : public IDevice
{
  public:
    explicit Device(const DeviceConfig& config);
    ~Device() override;

    // clang-format off
    // Buffer
    BufferPtr createBuffer(uint64_t byteSize, bool staging) override;
    BufferPtr createBuffer(const BufferDesc& desc) override;
    BufferPtr createHostBuffer(uint64_t byteSize) override;

    // Texture
    TexturePtr createTexture(const TextureDesc& desc) override;
    SamplerPtr createSampler(const SamplerDesc& desc) override { return nullptr; }
    SwapChainPtr createSwapChain(GLFWwindow* window, bool vsync) override;

    // Pipeline
    [[nodiscard]] BindlessTablePtr createBindlessTable(uint32_t size) override { return nullptr; }
    //[[nodiscard]] ShaderPtr createShader(const ShaderModule& shaderModule) const;
    [[nodiscard]] rhi::PipelinePtr createGraphicsPipeline(const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc) override;
    [[nodiscard]] rhi::PipelinePtr createComputePipeline(const ShaderModule& shaderModule) override;

    [[nodiscard]] rhi::PipelinePtr loadPipeline(const std::string& name, const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc) override { return nullptr; }

    // Execution
    void waitIdle() override {}
    [[nodiscard]] CommandPtr createCommand(QueueType type) override { return nullptr; }
    void submitCommand(CommandPtr& command) override {}
    void submitOneShot(const CommandPtr& command) override {}
    void runGarbageCollection() override {}

    StoragePtr getStorage() override { return nullptr; }

    [[nodiscard]] GraphicsAPI getGraphicsAPI() const override { return GraphicsAPI::METAL; }
    [[nodiscard]] const MetalContext& getContext() const { return m_context; }
    // clang-format on

  private:
    void populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const;

    MTL::Device* m_device = nullptr;
    MetalContext m_context;
};

DevicePtr CreateDevice(const DeviceConfig& config);

} // namespace ler::rhi::metal