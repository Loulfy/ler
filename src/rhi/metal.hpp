//
// Created by Loulfy on 21/11/2024.
//

#pragma once

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <metal_irconverter/metal_irconverter.h>

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
    IRRootSignature* rootSignature = nullptr;
    MTL::ComputePipelineState* encoder = nullptr;
};

struct ICBContainer
{
    NS::SharedPtr<MTL::IndirectCommandBuffer> icb;
    NS::SharedPtr<MTL::Buffer> bindingArgs;
    NS::SharedPtr<MTL::Buffer> drawArgs;
    NS::SharedPtr<MTL::Buffer> uniforms;
    NS::SharedPtr<MTL::Buffer> topLevel;
};

struct Buffer final : public IBuffer
{
    MTL::Buffer* handle = nullptr;
    MTL::Texture* view = nullptr;
    std::optional<ICBContainer> container;

    // clang-format off
    ~Buffer() override = default;
    explicit Buffer(const MetalContext& context) : m_context(context) {}
    [[nodiscard]] uint64_t sizeInBytes() const override { return handle->length(); }
    [[nodiscard]] bool staging() const override { return handle->storageMode() == MTL::StorageModeShared; }
    void uploadFromMemory(const void* src, uint64_t byteSize) const override;
    void getUint(uint32_t* ptr) const override;
    // clang-format on

  private:
    const MetalContext& m_context;
};

struct Texture final : public ITexture
{
    MTL::Texture* handle = nullptr;
    MTL::Texture* view = nullptr;

    // clang-format off
    ~Texture() override = default;
    explicit Texture(const MetalContext& context) : m_context(context) {}

    [[nodiscard]] Extent extent() const override { return {static_cast<uint32_t>(handle->width()), static_cast<uint32_t>(handle->height())}; }
    // clang-format on

  private:
    const MetalContext& m_context;
};

class Sampler final : public ISampler
{
public:

    NS::SharedPtr<MTL::SamplerDescriptor> desc;
    NS::SharedPtr<MTL::SamplerState> handle;
};

class BindlessTable : public CommonBindlessTable
{
public:
    BindlessTable(const MetalContext& context, uint32_t count);
    void setSampler(const SamplerPtr& sampler, uint32_t slot) override;

    static IRRootSignature* buildBindlessLayout();

    [[nodiscard]] std::array<MTL::Buffer*,2> heaps() const { return {m_resourceDescriptor.get(), m_samplerDescriptor.get()}; }
    [[nodiscard]] std::vector<MTL::Resource*> usedResources();
    [[nodiscard]] MTL::Buffer* resourceDescriptor() const { return m_resourceDescriptor.get(); }

private:
    const MetalContext& m_context;
    NS::SharedPtr<MTL::Buffer> m_resourceDescriptor;
    NS::SharedPtr<MTL::Buffer> m_samplerDescriptor;

    bool visitTexture(const TexturePtr& texture, uint32_t slot) override;
    bool visitBuffer(const BufferPtr& buffer, uint32_t slot) override;
};

class BasePipeline : public IPipeline
{
    friend class Device;

  public:
    enum PipelineType
    {
        PipelineTypeRender = 0,
        PipelineTypeCompute
    };
    // clang-format off
    explicit BasePipeline(const MetalContext& context) : m_context(context) {}
    void createDescriptorSet(uint32_t set) override {}
    void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, TexturePtr& texture) override {}
    void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, const std::span<TexturePtr>& textures) override {}
    void updateStorage(uint32_t descriptor, uint32_t binding, BufferPtr& buffer, uint64_t byteSize) override {}
    [[nodiscard]] PipelineType getPipelineType() const { return m_pipelineType; }
    [[nodiscard]] bool useIndirectBuffer() const { return m_useIndirectBuffer; }
    // clang-format on

    // Graphics
    NS::SharedPtr<MTL::RenderPipelineState> renderPipelineState;
    NS::SharedPtr<MTL::DepthStencilState> depthStencilState;
    MTL::TriangleFillMode fillMode = MTL::TriangleFillModeFill;
    // Compute
    NS::SharedPtr<MTL::ComputePipelineState> computePipelineState;
    MTL::Size threadGroupSize = {};

  private:
    const MetalContext& m_context;
    PipelineType m_pipelineType = PipelineTypeRender;
    bool m_useIndirectBuffer = false;
};

class GraphicsPipeline final : public BasePipeline
{
    friend class Device;

  public:
    explicit GraphicsPipeline(const MetalContext& context);

    NS::SharedPtr<MTL::RenderPipelineState> pipelineState;
    NS::SharedPtr<MTL::DepthStencilState> depthStencilState;
    MTL::TriangleFillMode fillMode = MTL::TriangleFillModeFill;
};

class ComputePipeline final : public BasePipeline
{
    friend class Device;

public:
    explicit ComputePipeline(const MetalContext& context);

    NS::SharedPtr<MTL::ComputePipelineState> pipelineState;
    MTL::Size threadGroupSize = {};
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
    void bindPipeline(const rhi::PipelinePtr& pipeline, const BindlessTablePtr& table) override;
    void setConstant(const BufferPtr& buffer, ShaderType stage) override;
    void pushConstant(const rhi::PipelinePtr& pipeline, ShaderType stage, uint32_t slot, const void* data, uint8_t size) override;
    void drawPrimitives(uint32_t vertexCount) const override;
    void drawIndexedPrimitives(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex, uint32_t instanceId) const override;
    void encodeIndirectIndexedPrimitives(const EncodeIndirectIndexedDrawDesc& desc) override;
    void drawIndirectIndexedPrimitives(const rhi::PipelinePtr& pipeline, const BufferPtr& commands, const BufferPtr& count, uint32_t maxDrawCount, uint32_t stride) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    void endRendering() const override;
    void beginRendering(const RenderingInfo& renderingInfo) override;
    void beginRendering(const rhi::PipelinePtr& pipeline, TexturePtr& backBuffer) override {}
    void addImageBarrier(const TexturePtr& texture, ResourceState new_state) const override {}
    void addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const override;
    void clearColorImage(const TexturePtr& texture, const std::array<float,4>& color) const override {}
    void copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub, const unsigned char* pSrcData) const override;
    void copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t byteSize, uint64_t dstOffset) override;
    void syncBuffer(const BufferPtr& dst, const void* src, uint64_t byteSize) override;
    void fillBuffer(const BufferPtr& dst, uint32_t value) const override;
    void bindIndexBuffer(const BufferPtr& indexBuffer) override;
    void bindVertexBuffers(uint32_t slot, const BufferPtr& indexBuffer) override;
    void beginDebugEvent(const std::string& label, const std::array<float, 4>& color) const override;
    void endDebugEvent() const override;
    [[nodiscard]] MTL::RenderCommandEncoder* renderEncoder() const { return m_renderCommandEncoder; }
    // clang-format on

  private:
    const MetalContext& m_context;
    NS::SharedPtr<MTL::RenderPassDescriptor> m_renderPassDescriptor;
    MTL::RenderCommandEncoder* m_renderCommandEncoder = nullptr;
    MTL::ComputeCommandEncoder* m_computeCommandEncoder = nullptr;
    MTL::Buffer* m_indexBuffer = nullptr;
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
    [[nodiscard]] Format format() const override;

    void createNativeSync();

    Device* device = nullptr;
    NS::SharedPtr<CA::MetalLayer> layer;
    uint32_t swapChainIndex = 0;
    uint32_t currentFrame = 0;
    // clang-format on

  private:
    uint32_t m_frameIndex = 0;
    NS::SharedPtr<MTL::SharedEvent> m_fence;
    dispatch_semaphore_t m_imageAcquireSemaphore = nil;
    std::array<uint64_t, FrameCount> m_fenceValues = {};
    std::array<TexturePtr, FrameCount> m_images;
    std::array<CommandPtr, FrameCount> m_command;
    const MetalContext& m_context;
};

struct ReadOnlyFile final : public IReadOnlyFile
{
    std::string getFilename() override;
    uint64_t sizeInBytes() override;

    fs::path path;
    MTL::IOFileHandle* handle = nullptr;
};

class Storage : public CommonStorage
{
public:
    Storage(Device* device, std::shared_ptr<coro::thread_pool>& tp);
    ReadOnlyFilePtr openFile(const fs::path& path) override;

private:
    const MetalContext& m_context;
    NS::SharedPtr<MTL::IOCommandQueue> m_queue;
    std::vector<std::byte*> m_buffers;

    coro::task<> makeSingleTextureTask(coro::latch& latch, BindlessTablePtr table, ReadOnlyFilePtr file) override;
    coro::task<> makeMultiTextureTask(coro::latch& latch, BindlessTablePtr table, std::vector<ReadOnlyFilePtr> files) override;
    coro::task<> makeBufferTask(coro::latch& latch, ReadOnlyFilePtr file, BufferPtr buffer, uint64_t fileLength, uint64_t fileOffset) override;
};

class ImGuiPass : public IRenderPass
{
  public:
    ~ImGuiPass() override;
    void begin(TexturePtr& backBuffer) override;
    void create(const DevicePtr& device, const SwapChainPtr& swapChain) override;
    void render(TexturePtr& backBuffer, CommandPtr& command) override;

  private:
    MTL::RenderPassDescriptor* m_renderPassDescriptor = nullptr;
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
    SamplerPtr createSampler(const SamplerDesc& desc) override;
    SwapChainPtr createSwapChain(GLFWwindow* window, bool vsync) override;

    // Pipeline
    [[nodiscard]] BindlessTablePtr createBindlessTable(uint32_t size) override;
    //[[nodiscard]] ShaderPtr createShader(const ShaderModule& shaderModule) const;
    [[nodiscard]] rhi::PipelinePtr createGraphicsPipeline(const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc) override;
    [[nodiscard]] rhi::PipelinePtr createComputePipeline(const ShaderModule& shaderModule) override;

    [[nodiscard]] rhi::PipelinePtr loadPipeline(const std::string& name, const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc) override { return nullptr; }

    // Execution
    void waitIdle() override {}
    [[nodiscard]] CommandPtr createCommand(QueueType type) override;
    void submitCommand(CommandPtr& command) override {}
    void submitOneShot(const CommandPtr& command) override;
    void runGarbageCollection() override {}

    StoragePtr getStorage() override { return m_storage; }

    [[nodiscard]] GraphicsAPI getGraphicsAPI() const override { return GraphicsAPI::METAL; }
    [[nodiscard]] const MetalContext& getContext() const { return m_context; }

    static MTL::PixelFormat convertFormat(Format format);
    static Format reverseFormat(MTL::PixelFormat mtlFormat);

    // clang-format on

  private:
    void populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const;

    MTL::Device* m_device = nullptr;
    MetalContext m_context;
    std::unique_ptr<Queue> m_queue;
    std::shared_ptr<coro::thread_pool> m_threadPool;
    std::shared_ptr<IStorage> m_storage;
    NS::SharedPtr<MTL::ComputePipelineState> m_indirectComputeEncoder;
    NS::SharedPtr<MTL::ArgumentEncoder> m_indirectArgumentEncoder;
    NS::SharedPtr<MTL::Buffer> m_pushConstantBuffer;
};

DevicePtr CreateDevice(const DeviceConfig& config);

} // namespace ler::rhi::metal