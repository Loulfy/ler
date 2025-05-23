//
// Created by Loulfy on 30/11/2023.
//

#pragma once

#include <filesystem>
#include <memory>
#include <utility>
namespace fs = std::filesystem;

#include "enum.hpp"
#include "log/log.hpp"
#include "sys/utils.hpp"

#include <coro/coro.hpp>
namespace async = coro;

#include <imgui.h>

struct GLFWwindow;

namespace ler::rhi
{
template <typename T> T align(T size, T alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

using Latch = std::shared_ptr<coro::latch>;

class IResource
{
  protected:
    IResource() = default;
    virtual ~IResource() = default;

  public:
    // Non-copyable and non-movable
    IResource(const IResource&) = delete;
    IResource(const IResource&&) = delete;
    IResource& operator=(const IResource&) = delete;
    IResource& operator=(const IResource&&) = delete;
    ResourceState state = ResourceState::Undefined;
};

struct BufferDesc
{
    uint64_t sizeInBytes = 0;
    uint32_t alignment = 0;
    uint32_t stride = 0;
    Format format = Format::UNKNOWN;
    std::string debugName;

    bool isUAV = false;
    bool isICB = false;
    bool isManaged = false;
    bool isStaging = false;
    bool isReadBack = false;
    bool isVertexBuffer = false;
    bool isIndexBuffer = false;
    bool isConstantBuffer = false;
    bool isDrawIndirectArgs = false;
    bool isAccelStructBuildInput = false;
};

struct IBuffer : IResource
{
    [[nodiscard]] virtual uint64_t sizeInBytes() const = 0;
    [[nodiscard]] virtual bool staging() const = 0;
    virtual void uploadFromMemory(const void* src, uint64_t sizeInBytes) const = 0;
    virtual void getUint(uint32_t* ptr) const = 0;
};

using BufferPtr = std::shared_ptr<IBuffer>;

struct Extent
{
    uint32_t width = 0u;
    uint32_t height = 0u;
};

struct FormatBlockInfo
{
    unsigned int blockSizeByte = 0u;
    unsigned int blockWidth = 0u;  // in texels
    unsigned int blockHeight = 0u; // in texels
};

struct Subresource
{
    uint32_t index = 0u;
    uint32_t depth = 1u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t offset = 0u;
    uint32_t rowPitch = 0u;
    uint64_t slicePitch = 0u;
};

struct TextureDesc
{
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t arrayLayers = 1;
    uint32_t mipLevels = 1;
    uint32_t sampleCount = 1;
    uint32_t sampleQuality = 0;
    Format format = Format::UNKNOWN;
    uint8_t dimension = 2u;
    std::string debugName;

    bool isRenderTarget = false;
    bool isTiled = false;
    bool isUAV = false;
};

struct ITexture : IResource
{
    [[nodiscard]] virtual Extent extent() const = 0;
};

using TexturePtr = std::shared_ptr<ITexture>;

struct SamplerDesc
{
    bool filter = true;
    SamplerAddressMode addressU = SamplerAddressMode::Clamp;
    SamplerAddressMode addressV = SamplerAddressMode::Clamp;
    SamplerAddressMode addressW = SamplerAddressMode::Clamp;
};

class ISampler : public IResource
{
};

using SamplerPtr = std::shared_ptr<ISampler>;

using ResourcePtr = std::variant<TexturePtr, BufferPtr>;

class IBindlessTable;
class ShaderResourceView
{
    uint32_t m_index = 0u;
    // ResourcePtr m_resource;
    IBindlessTable* m_allocator = nullptr;

  public:
    friend class CommonBindlessTable;
    ~ShaderResourceView();
    void destroy();
    [[nodiscard]] uint32_t getBindlessIndex() const;
};

using ResourceViewPtr = std::shared_ptr<ShaderResourceView>;

class IBindlessTable
{
  public:
    virtual ~IBindlessTable() = default;
    virtual void setSampler(const SamplerPtr& sampler, uint32_t slot) = 0;
    [[nodiscard]] virtual ResourceViewPtr createResourceView(const ResourcePtr& res) = 0;
    [[nodiscard]] virtual TexturePtr getTexture(uint32_t slot) const = 0;
    [[nodiscard]] virtual BufferPtr getBuffer(uint32_t slot) const = 0;
    [[nodiscard]] virtual std::lock_guard<std::mutex> lock() = 0;
    [[nodiscard]] virtual uint32_t newBindlessIndex() = 0;
    virtual void freeBindlessIndex(uint32_t index) = 0;
};

using BindlessTablePtr = std::shared_ptr<IBindlessTable>;

using PipelineRenderingAttachment = std::vector<Format>;

struct ShaderModule
{
    fs::path path;
    std::string entryPoint = "main";
    ShaderType stage = ShaderType::None;
    GraphicsAPI backend = GraphicsAPI::VULKAN;
    std::string name;
};

struct PipelineDesc
{
    Extent viewport;
    PrimitiveType topology = PrimitiveType::TriangleList;
    RasterFillMode fillMode = RasterFillMode::Solid;
    uint32_t sampleCount = 1u;
    uint32_t textureCount = 1u;
    bool writeDepth = false;
    float lineWidth = 1.f;
    PipelineRenderingAttachment colorAttach;
    Format depthAttach = Format::D32;
    bool indirectDraw = false;
};

struct Attachment
{
    TexturePtr texture;
    AttachmentLoadOp loadOp = AttachmentLoadOp::DontCare;
};

struct RenderingInfo
{
    std::array<Attachment, 8> colors;
    Attachment depth;
    uint32_t colorCount = 0;
    Extent viewport;
};

class IPipeline
{
  public:
    virtual ~IPipeline() = default;
    /// @deprecated
    virtual void createDescriptorSet(uint32_t set) = 0;
    virtual void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, TexturePtr& texture) = 0;
    virtual void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler,
                               const std::span<TexturePtr>& textures) = 0;
    virtual void updateStorage(uint32_t descriptor, uint32_t binding, BufferPtr& buffer, uint64_t byteSize) = 0;
};

using PipelinePtr = std::shared_ptr<IPipeline>;

struct PsoCache
{
    std::string name;
    PipelineDesc desc;
    PipelinePtr pipeline;
    std::vector<ShaderModule> modules;
};

struct EncodeIndirectIndexedDrawDesc
{
    BufferPtr drawsBuffer;
    BufferPtr countBuffer;
    BufferPtr constantBuffer;
    BufferPtr indexBuffer;
    std::array<BufferPtr, 4> vertexBuffer;
    BindlessTablePtr table;
    uint32_t maxDrawCount = 0;
};

class ICommand
{
  public:
    virtual ~ICommand() = default;
    virtual void reset() = 0;
    virtual void bindPipeline(const PipelinePtr& pipeline, uint32_t descriptorHandle) const = 0;
    virtual void bindPipeline(const PipelinePtr& pipeline, const BindlessTablePtr& table,
                              const BufferPtr& constantBuffer) = 0;
    virtual void setConstant(const BufferPtr& buffer, ShaderType stage) = 0;
    virtual void pushConstant(const PipelinePtr& pipeline, ShaderType stage, uint32_t slot, const void* data,
                              uint8_t size) = 0;
    virtual void drawPrimitives(uint32_t vertexCount) const = 0;
    virtual void drawIndexedPrimitives(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex,
                                       uint32_t instanceId) const = 0;
    virtual void encodeIndirectIndexedPrimitives(const EncodeIndirectIndexedDrawDesc& desc) = 0;
    virtual void drawIndirectIndexedPrimitives(const PipelinePtr& pipeline, const BufferPtr& commands,
                                               const BufferPtr& count, uint32_t maxDrawCount, uint32_t stride) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void endRendering() const = 0;
    virtual void beginRendering(const RenderingInfo& renderingInfo) = 0;
    virtual void beginRendering(const PipelinePtr& pipeline, TexturePtr& backBuffer) = 0;
    virtual void addImageBarrier(const TexturePtr& texture, ResourceState new_state) const = 0;
    virtual void addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const = 0;
    virtual void clearColorImage(const TexturePtr& texture, const std::array<float, 4>& color) const = 0;
    virtual void copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub,
                                     const unsigned char* pSrcData) const = 0;
    virtual void copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t sizeInBytes, uint64_t dstOffset) = 0;
    virtual void syncBuffer(const BufferPtr& dst, const void* src, uint64_t sizeInBytes) = 0;
    virtual void fillBuffer(const BufferPtr& dst, uint32_t value) const = 0;
    virtual void bindIndexBuffer(const BufferPtr& indexBuffer) = 0;
    virtual void bindVertexBuffers(uint32_t slot, const BufferPtr& indexBuffer) = 0;
    virtual void beginDebugEvent(const std::string& label, const std::array<float, 4>& color) const = 0;
    virtual void endDebugEvent() const = 0;

    uint64_t submissionID = 0;
    QueueType queueType = QueueType::Graphics;
    std::vector<std::shared_ptr<IResource>> referencedResources;
};

using CommandPtr = std::shared_ptr<ICommand>;

using RenderPass = std::function<void(TexturePtr&, CommandPtr&)>;

struct ISwapChain
{
    virtual ~ISwapChain() = default;
    virtual uint32_t present(const RenderPass& renderPass) = 0;
    virtual void resize(uint32_t width, uint32_t height, bool vsync) = 0;
    [[nodiscard]] virtual Extent extent() const = 0;
    [[nodiscard]] virtual Format format() const = 0;

    static constexpr uint32_t FrameCount = 3;
};

using SwapChainPtr = std::shared_ptr<ISwapChain>;

class IReadOnlyFile
{
  public:
    virtual ~IReadOnlyFile() = default;
    virtual std::string getFilename() = 0;
    virtual uint64_t sizeInBytes() = 0;
};

using ReadOnlyFilePtr = std::shared_ptr<IReadOnlyFile>;

struct TextureStreaming
{
    ResourceViewPtr view;
    std::string stem;
};

using TextureStreamingBatch = std::vector<TextureStreaming>;

struct StorageError
{
};

struct TextureStreamingMetadata
{
    TextureDesc desc;
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
    ReadOnlyFilePtr file;
};

class IStorage
{
  public:
    virtual ~IStorage() = default;
    virtual void update() = 0;
    virtual ReadOnlyFilePtr openFile(const fs::path& path) = 0;
    virtual std::vector<ReadOnlyFilePtr> openFiles(const fs::path& path, const fs::path& ext) = 0;
    virtual void requestLoadTexture(coro::latch& latch, BindlessTablePtr& table,
                                    const std::span<ReadOnlyFilePtr>& files) = 0;
    virtual void requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer,
                                   uint64_t fileLength, uint64_t fileOffset) = 0;
    virtual void requestOpenTexture(coro::latch& latch, BindlessTablePtr& table, const std::span<fs::path>& paths) = 0;
    virtual void requestLoadTexture(coro::latch& latch, BindlessTablePtr& table,
                                    const std::span<TextureStreamingMetadata>& textures) = 0;
    virtual std::expected<ResourceViewPtr, StorageError> getResource(uint64_t pathKey) = 0;
};

using StoragePtr = std::shared_ptr<IStorage>;

struct DeviceConfig
{
    bool debug = true;
    bool hostBuffer = true;
    std::vector<const char*> extensions;
};

class IDevice
{
  public:
    virtual ~IDevice() = default;

    // Buffer
    virtual BufferPtr createBuffer(uint64_t sizeInBytes, bool staging) = 0;
    virtual BufferPtr createBuffer(const BufferDesc& desc) = 0;
    virtual BufferPtr createHostBuffer(uint64_t sizeInBytes) = 0;

    // Texture
    virtual TexturePtr createTexture(const TextureDesc& desc) = 0;
    virtual SamplerPtr createSampler(const SamplerDesc& desc) = 0;
    virtual SwapChainPtr createSwapChain(GLFWwindow* window, bool vsync) = 0;

    // Pipeline
    void shaderAutoCompile();
    static void compileShader(const ShaderModule& shaderModule, const fs::path& output);
    [[nodiscard]] virtual BindlessTablePtr createBindlessTable(uint32_t count) = 0;

    [[nodiscard]] virtual PipelinePtr createGraphicsPipeline(const std::span<ShaderModule>& shaderModules,
                                                             const PipelineDesc& desc) = 0;
    [[nodiscard]] virtual PipelinePtr createComputePipeline(const ShaderModule& shaderModule) = 0;

    [[nodiscard]] virtual PipelinePtr loadPipeline(const std::string& name,
                                                   const std::span<ShaderModule>& shaderModules,
                                                   const PipelineDesc& desc) = 0;

    [[nodiscard]] virtual GraphicsAPI getGraphicsAPI() const = 0;

    // Execution
    virtual void waitIdle() = 0;
    [[nodiscard]] virtual CommandPtr createCommand(QueueType type) = 0;
    virtual void submitCommand(CommandPtr& command) = 0;
    virtual void submitOneShot(const CommandPtr& command) = 0;
    virtual void runGarbageCollection() = 0;
    virtual void beginFrame(uint32_t frameIndex) = 0;

    virtual StoragePtr getStorage() = 0;
};

using DevicePtr = std::shared_ptr<IDevice>;

class IRenderPass
{
  public:
    virtual ~IRenderPass() = default;
    virtual void create(const DevicePtr& device, const SwapChainPtr& swapChain) = 0;
    virtual void render(TexturePtr& backBuffer, CommandPtr& command) = 0;
    // clang-format off
    virtual void begin(TexturePtr& backBuffer) { }
    virtual void resize(const DevicePtr& device, const Extent& viewport) { }
    [[nodiscard]] virtual bool startup() const { return true; }
    // clang-format on
};

class ScratchBuffer
{
  public:
    explicit ScratchBuffer(IDevice* device);
    uint64_t allocate(uint32_t sizeInByte);
    [[nodiscard]] const BufferPtr& getBuffer() const;
    void reset();

  private:
    static constexpr uint64_t m_capacity = sys::C08Mio;
    std::atomic_uint64_t m_size = 0;
    BufferPtr m_staging;
};

struct StringFormatMapping
{
    Format format = Format::UNKNOWN;
    std::string name;
};

FormatBlockInfo formatToBlockInfo(Format format);
Format stringToFormat(const std::string& name);
std::string to_string(ShaderType stageType);
std::string to_string(ResourceState state);
std::string to_string(Format format);

void from_json(const json& j, PsoCache& p);
void from_json(const json& j, ShaderModule& s);
void to_json(json& j, const PsoCache& p);
} // namespace ler::rhi
