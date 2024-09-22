//
// Created by loulfy on 30/11/2023.
//

#pragma once

#include <set>
#include <map>
#include <latch>
#include <ranges>
#include <memory>
#include <limits>
#include <filesystem>
#include <unordered_map>
#include <memory_resource>
#include <utility>
namespace fs = std::filesystem;

#include "enum.hpp"
#include "log/log.hpp"
#include "sys/utils.hpp"
#include "sys/file.hpp"

#include <coro/coro.hpp>
namespace async = coro;

#include <xxh3.h>
#include <imgui.h>

struct GLFWwindow;

struct Path
{
    std::string s;
};

namespace std
{
    template<> struct hash<Path>
    {
        std::size_t operator()(Path const& p) const noexcept
        {
            XXH64_hash_t hash = XXH3_64bits(p.s.data(), p.s.size());
            return hash;
        }
    };
}

namespace ler::rhi
{
    template<typename T> T align(T size, T alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

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

    /*class Latch
    {
      public:
        explicit Latch(std::int64_t count) noexcept : m_count(count) {}

        Latch(const Latch&)                    = delete;
        Latch(Latch&&)                         = delete;
        auto operator=(const Latch&) -> Latch& = delete;
        auto operator=(Latch&&) -> Latch&      = delete;

        auto reset(std::int64_t count) noexcept -> void { m_count.store(count); }
        auto is_ready() const noexcept -> bool { return m_count.load(std::memory_order::acquire) == 0; }
        auto remaining() const noexcept -> std::size_t { return m_count.load(std::memory_order::acquire); }
        auto count_down(std::int64_t n = 1) noexcept -> bool { return m_count.fetch_sub(n, std::memory_order::acq_rel) <= n; }
      private:
        std::atomic_int64_t m_count;
    };*/

    struct BufferDesc
    {
        uint32_t byteSize = 0;
        uint32_t alignment = 0;
        std::string debugName;

        bool isUAV = false;
        bool isStaging = false;
        bool isVertexBuffer = false;
        bool isIndexBuffer = false;
        bool isConstantBuffer = false;
        bool isDrawIndirectArgs = false;
        bool isAccelStructBuildInput = false;
    };

    struct IBuffer : public IResource
    {
        [[nodiscard]] virtual uint32_t sizeBytes() const = 0;
        [[nodiscard]] virtual bool staging() const = 0;
        virtual void uploadFromMemory(const void* src, uint32_t byteSize) const = 0;
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
        unsigned int blockWidth = 0u;    // in texels
        unsigned int blockHeight = 0u;   // in texels
    };

    struct Subresource
    {
        uint32_t index = 0u;
        uint32_t depth = 1u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t offset = 0u;
        uint32_t rowPitch = 0u;
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

    struct ITexture : public IResource
    {
    public:

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

    using ResourcePtr = std::variant<TexturePtr,BufferPtr>;
    class IBindlessTable
    {
      public:
        virtual ~IBindlessTable() = default;
        virtual uint32_t allocate() = 0;
        virtual bool setResource(const ResourcePtr& res, uint32_t slot) = 0;
        virtual void setSampler(const SamplerPtr& sampler, uint32_t slot) = 0;
        [[nodiscard]] virtual TexturePtr getTexture(uint32_t slot) const = 0;
        [[nodiscard]] virtual BufferPtr getBuffer(uint32_t slot) const = 0;
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
    };

    struct Attachment
    {
        TexturePtr texture;
        AttachmentLoadOp loadOp = AttachmentLoadOp::DontCare;
    };

    struct RenderingInfo
    {
        std::array<Attachment,8> colors;
        Attachment depth;
        uint32_t colorCount = 0;
        Extent viewport;
    };

    class IPipeline
    {
    public:
        virtual ~IPipeline() = default;
        virtual void createDescriptorSet(uint32_t set) = 0;
        virtual void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, TexturePtr& texture) = 0;
        virtual void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, const std::span<TexturePtr>& textures) = 0;
        virtual void updateStorage(uint32_t descriptor, uint32_t binding, BufferPtr& buffer, uint64_t byteSize) = 0;
    };

    using PipelinePtr = std::shared_ptr<IPipeline>;

    class ICommand
    {
    public:
        virtual ~ICommand() = default;
        virtual void reset() = 0;
        virtual void bindPipeline(const PipelinePtr& pipeline, uint32_t descriptorHandle) const = 0;
        virtual void bindPipeline(const PipelinePtr& pipeline, const BindlessTablePtr& table) const = 0;
        virtual void pushConstant(const PipelinePtr& pipeline, const void* data, uint8_t size) const = 0;
        virtual void drawIndexed(uint32_t vertexCount) const = 0;
        virtual void drawIndexedInstanced(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex, uint32_t firstId) const = 0;
        virtual void drawIndirectIndexed(const PipelinePtr& pipeline, const BufferPtr& commands, const BufferPtr& count, uint32_t maxDrawCount, uint32_t stride) const = 0;
        virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) const = 0;
        virtual void endRendering() const = 0;
        virtual void beginRendering(const RenderingInfo& renderingInfo) const = 0;
        virtual void beginRendering(const PipelinePtr& pipeline, TexturePtr& backBuffer) = 0;
        virtual void addImageBarrier(const TexturePtr& texture, ResourceState new_state) const = 0;
        virtual void addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const = 0;
        virtual void clearColorImage(const TexturePtr& texture, const std::array<float,4>& color) const = 0;
        virtual void copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub, const unsigned char* pSrcData) const = 0;
        virtual void copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t byteSize, uint64_t dstOffset) = 0;
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

    using RenderPass = std::function<void(TexturePtr&,CommandPtr&)>;

    struct ISwapChain
    {
    public:
        virtual ~ISwapChain() = default;
        virtual uint32_t present(const RenderPass& renderPass) = 0;
        virtual void resize(uint32_t width, uint32_t height, bool vsync) = 0;
        [[nodiscard]] virtual Extent extent() const = 0;
        [[nodiscard]] virtual Format format() const { return Format::RGBA8_UNORM; }

        static constexpr uint32_t FrameCount = 3;
    };

    using SwapChainPtr = std::shared_ptr<ISwapChain>;

    class IReadOnlyFile
    {
    public:
        virtual ~IReadOnlyFile() = default;
        virtual std::string getFilename() = 0;
        virtual uint32_t sizeBytes() = 0;
    };

    using ReadOnlyFilePtr = std::shared_ptr<IReadOnlyFile>;

    struct Result
    {
        bool succeeded = false;
        std::string error;
        uint32_t slot = 0;
    };

    class IStorage
    {
    public:

        virtual ~IStorage() = default;
        virtual void update() = 0;
        virtual ReadOnlyFilePtr openFile(const fs::path& path) = 0;
        virtual std::vector<ReadOnlyFilePtr> openFiles(const fs::path& path, const fs::path& ext) = 0;
        virtual void requestLoadTexture(coro::latch& latch, BindlessTablePtr& table, const std::span<ReadOnlyFilePtr>& files) = 0;
        virtual void requestLoadBuffer(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer, uint32_t fileLength, uint32_t fileOffset) = 0;
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
        virtual BufferPtr createBuffer(uint32_t byteSize, bool staging) = 0;
        virtual BufferPtr createBuffer(const BufferDesc& desc) = 0;
        virtual BufferPtr createHostBuffer(uint32_t byteSize) = 0;

        // Texture
        virtual TexturePtr createTexture(const TextureDesc& desc) = 0;
        virtual SamplerPtr createSampler(const SamplerDesc& desc) = 0;
        virtual SwapChainPtr createSwapChain(GLFWwindow* window, bool vsync) = 0;

        // Pipeline
        void shaderAutoCompile();
        void compileShader(const ShaderModule& shaderModule, const fs::path& output);
        [[nodiscard]] virtual BindlessTablePtr createBindlessTable(uint32_t count) = 0;

        [[nodiscard]] virtual PipelinePtr createGraphicsPipeline(const std::vector<ShaderModule>& shaderModules, const PipelineDesc& desc) = 0;
        [[nodiscard]] virtual PipelinePtr createComputePipeline(const ShaderModule& shaderModule) = 0;

        [[nodiscard]] virtual GraphicsAPI getGraphicsAPI() const = 0;

        // Execution
        virtual void waitIdle() = 0;
        [[nodiscard]] virtual CommandPtr createCommand(QueueType type) = 0;
        virtual void submitCommand(CommandPtr& command) = 0;
        virtual void submitOneShot(const CommandPtr& command) = 0;
        virtual void runGarbageCollection() = 0;

        virtual StoragePtr getStorage() = 0;
    };

    using DevicePtr = std::shared_ptr<IDevice>;

    using Latch = std::shared_ptr<coro::latch>;

    class IRenderPass
    {
    public:

        virtual ~IRenderPass() = default;
        virtual void create(const DevicePtr& device, const SwapChainPtr& swapChain) = 0;
        virtual void render(TexturePtr& backBuffer, CommandPtr& command) = 0;
        virtual void begin() {}
        virtual void resize(const DevicePtr& device, const Extent& viewport) {}
        [[nodiscard]] virtual bool startup() const { return true; }
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
}
