//
// Created by loulfy on 01/12/2023.
//

#pragma once

#include "rhi.hpp"
#include "queue.hpp"
#include "storage.hpp"
#include "sys/mem.hpp"

#include <d3d12.h>
#include <dstorage.h>
#include <dxgi1_6.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <D3D12MemAlloc.h>
#include <pix3.h>

namespace ler::rhi::d3d12
{
    using Microsoft::WRL::ComPtr;

    std::string getErrorMsg(HRESULT hr);

    struct DescriptorPool
    {
        D3D12_DESCRIPTOR_HEAP_TYPE type = {};
        uint32_t count = 0u;
    };

    static const std::array<DescriptorPool, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> c_DescriptorPoolSize =
    {{
        {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8192},
        {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16},
        {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64},
        {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16},
    }};

    class DescriptorHeapAllocator;
    class DescriptorHeapAllocation
    {
    public:

        void reset();
        DescriptorHeapAllocation() = default;
        DescriptorHeapAllocation(DescriptorHeapAllocator* pAllocator,
                                 ID3D12DescriptorHeap* pHeap,
                                 D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle,
                                 D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle,
                                 uint32_t descriptorSize,
                                 uint32_t NHandles) noexcept;

        ~DescriptorHeapAllocation();
        // Move constructor/assignment (copy is not allowed)
        DescriptorHeapAllocation(DescriptorHeapAllocation&& alloc) noexcept;
        DescriptorHeapAllocation& operator=(DescriptorHeapAllocation&& alloc) noexcept;
        DescriptorHeapAllocation(const DescriptorHeapAllocation&) = delete;
        DescriptorHeapAllocation& operator=(const DescriptorHeapAllocation&) = delete;

        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE getCpuHandle(uint32_t offset = 0) const;
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE getGpuHandle(uint32_t offset = 0) const;

        [[nodiscard]] uint32_t getNumHandles() const { return m_numHandles; }
        [[nodiscard]] bool isNull() const { return m_firstCpuHandle.ptr == 0; }
        [[nodiscard]] ID3D12DescriptorHeap* heap() const { return m_pDescriptorHeap; }

    private:

        // First CPU descriptor handle in this allocation
        D3D12_CPU_DESCRIPTOR_HANDLE m_firstCpuHandle = {};

        // First GPU descriptor handle in this allocation
        D3D12_GPU_DESCRIPTOR_HANDLE m_firstGpuHandle = {};

        DescriptorHeapAllocator* m_pAllocator = nullptr;
        ID3D12DescriptorHeap* m_pDescriptorHeap = nullptr;

        // Number of descriptors in the allocation
        uint32_t m_numHandles = 0;

        // Descriptor size
        uint32_t m_descriptorSize = 0;
    };

    class DescriptorHeapAllocator
    {
    public:

        void create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count, bool shaderVisible = true);
        DescriptorHeapAllocation allocate(uint32_t count);
        void free(DescriptorHeapAllocation&& Allocation);

        [[nodiscard]] ID3D12DescriptorHeap* heap() const { return m_heap.Get(); }

    private:

        D3D12_DESCRIPTOR_HEAP_DESC desc;
        ComPtr<ID3D12DescriptorHeap> m_heap;
        UINT m_descriptorSize = 0u;

        sys::VariableSizeAllocator m_freeBlockManager;
    };

    struct D3D12Context
    {
        ID3D12Device* device = nullptr;
        IDStorageFactory* storage = nullptr;
        ID3D12CommandQueue* queue = nullptr;
        std::array<std::unique_ptr<DescriptorHeapAllocator>,D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> descriptorPool;
        std::unique_ptr<DescriptorHeapAllocator> descriptorPoolCpuOnly;
    };

    struct DxgiFormatMapping
    {
        Format abstractFormat;
        DXGI_FORMAT resourceFormat;
        DXGI_FORMAT srvFormat;
        DXGI_FORMAT rtvFormat;
    };

    const DxgiFormatMapping& getDxgiFormatMapping(Format abstractFormat);
    const DxgiFormatMapping& getDxgiFormatMapping(DXGI_FORMAT d3d12Format);

    struct Buffer : public IBuffer
    {
        ID3D12Resource* handle;
        D3D12_RESOURCE_DESC desc;
        D3D12MA::Allocation* allocation = nullptr;
        D3D12MA::ALLOCATION_DESC allocDesc = {};
        DescriptorHeapAllocation clearCpuHandle;
        DescriptorHeapAllocation clearGpuHandle;
        bool isCBV = false;

        ~Buffer() override { allocation->Release(); }
        [[nodiscard]] uint32_t sizeBytes() const override { return desc.Width; }
        [[nodiscard]] bool staging() const override { return allocDesc.HeapType & D3D12_HEAP_TYPE_UPLOAD; }
        void uploadFromMemory(const void* src, uint32_t byteSize) const override;
    };

    struct Texture : public ITexture
    {
        ID3D12Resource* handle;
        D3D12_RESOURCE_DESC desc;
        D3D12MA::Allocation* allocation = nullptr;
        D3D12MA::ALLOCATION_DESC allocDesc = {};
        DescriptorHeapAllocation rtvDescriptor;
        ComPtr<ID3D12Heap> dHeap;

        ~Texture() override { if(allocation) allocation->Release(); }

        [[nodiscard]] Extent extent() const override { return {uint32_t(desc.Width), desc.Height}; }
    };

    struct Sampler : public ISampler
    {
        D3D12_SAMPLER_DESC desc;
        DescriptorHeapAllocation descriptor;
    };

    struct ShaderBindDesc
    {
        D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        uint32_t bindPoint = 0;
        uint32_t bindCount = 1;
        uint32_t stride = 1;
        std::string name;
    };

    struct Shader
    {
        ComPtr<IDxcBlob> bytecode;
        ShaderType stage = ShaderType::None;
        std::vector<CD3DX12_DESCRIPTOR_RANGE1> rangesCbvSrvUav;
        std::vector<CD3DX12_DESCRIPTOR_RANGE1> rangesSampler;
        std::vector<std::string> inputElementSemanticNames;
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs;
        std::multimap<uint32_t, ShaderBindDesc> bindingMap;
    };

    using ShaderPtr = std::shared_ptr<Shader>;

    struct DescriptorSet
    {
        std::array<ID3D12DescriptorHeap*,2> heaps;
        std::array<DescriptorHeapAllocation,2> tables;
    };

    struct Pipeline : public IPipeline
    {
    public:

        friend class Device;
        using DescriptorRanges = std::vector<CD3DX12_DESCRIPTOR_RANGE1>;

        explicit Pipeline(const D3D12Context& context) : m_context(context) {}
        ComPtr<ID3D12RootSignature> rootSignature;
        ComPtr<ID3D12CommandSignature> commandSignature;
        ComPtr<ID3D12PipelineState> pipelineState;
        std::multimap<uint32_t, ShaderBindDesc> bindingMap;

        void merge(int type, DescriptorRanges& ranges);
        void initRootSignature();

        void createDescriptorSet(uint32_t set) override;
        void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, TexturePtr& texture) override;
        void updateStorage(uint32_t descriptor, uint32_t binding, BufferPtr& buffer, uint64_t byteSize) override;
        void updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, const std::span<TexturePtr>& textures) override;

        [[nodiscard]] DescriptorSet& getDescriptorSet(uint32_t id) { return m_descriptors[id]; }
        [[nodiscard]] bool isGraphics() const { return m_graphics; }

    private:

        // only shader visible heap descriptor (CBV_UAV_SRV & SAMPLER)
        std::array<DescriptorRanges, 2> m_ranges;
        std::vector<DescriptorSet> m_descriptors;

        const D3D12Context& m_context;
        bool m_graphics = true;
    };

    class Command : public ICommand
    {
    public:

        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator;

        void reset() override;
        void bindPipeline(const PipelinePtr& pipeline, uint32_t descriptorHandle) const override;
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
    };

    class Queue : public rhi::Queue
    {
    public:

        friend class Device;
        using CommandPtr = std::shared_ptr<Command>;
        Queue(const D3D12Context& context, QueueType queueID);

        uint64_t updateLastFinishedID() override;

        // submission
        uint64_t submit(const std::span<CommandPtr>& ppCmd);

        ComPtr<ID3D12Fence> fence;

    private:

        rhi::CommandPtr createCommandBuffer() override;

        const D3D12Context& m_context;
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        D3D12_COMMAND_LIST_TYPE m_commandType = {};
    };

    struct SwapChain : public ISwapChain
    {
    public:

        explicit SwapChain(const D3D12Context& context) : m_context(context) { }
        uint32_t present(const RenderPass& renderPass) override;
        void resize(uint32_t width, uint32_t height) override;
        [[nodiscard]] Extent extent() const override;

        void createNativeSync();

        ComPtr<IDXGISwapChain3> handle;
        std::array<ComPtr<ID3D12Resource>,FrameCount> renderTargets;

    private:

        UINT m_frameIndex = 0;
        HANDLE m_fenceEvent = {};
        ComPtr<ID3D12Fence> m_fence;
        ComPtr<ID3D12CommandQueue> m_commandQueue;
        std::array<UINT64,FrameCount> m_fenceValues = {};
        std::array<TexturePtr,FrameCount> m_images;
        std::array<CommandPtr,FrameCount> m_command;
        const D3D12Context& m_context;
    };

    struct StorageRequest
    {
        ComPtr<IDStorageStatusArray> status;
        std::coroutine_handle<> handle;
    };

    struct ReadOnlyFile : public IReadOnlyFile
    {
        std::string getFilename() override { return filename; }
        uint32_t sizeBytes() override {
            BY_HANDLE_FILE_INFORMATION info;
            handle->GetFileInformation(&info);
            return info.nFileSizeLow; }

        ComPtr<IDStorageFile> handle;
        std::string filename = "Unknown";
    };

    class Device;
    class Storage : public CommonStorage
    {
      public:
        Storage(Device* device, std::shared_ptr<coro::thread_pool>& tp);
        ReadOnlyFilePtr openFile(const fs::path& path) override;

      private:
        const D3D12Context& m_context;
        ComPtr<IDStorageQueue> queue;
        std::vector<std::pair<ID3D12Resource*,std::byte*>> buffers;

        void submitWait();
        coro::task<> makeSingleTextureTask(coro::latch& latch, TexturePoolPtr texturePool, ReadOnlyFilePtr file) override;
        coro::task<> makeMultiTextureTask(coro::latch& latch, TexturePoolPtr texturePool, std::vector<ReadOnlyFilePtr> files) override;
        coro::task<> makeBufferTask(coro::latch& latch, const ReadOnlyFilePtr& file, BufferPtr& buffer, uint32_t fileLength, uint32_t fileOffset) override;
    };

    class Device : public IDevice
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

        [[nodiscard]] GraphicsAPI getGraphicsAPI() const override { return GraphicsAPI::D3D12; }
        [[nodiscard]] const D3D12Context& getContext() const { return m_context; }

        static DXGI_FORMAT convertFormatRtv(Format format);
        static DXGI_FORMAT convertFormatRes(Format format);

        [[nodiscard]] ComPtr<ID3D12CommandQueue> getGraphicsQueue() const { return m_queues[int(QueueType::Graphics)]->m_commandQueue; }

        static D3D12_RESOURCE_STATES util_to_d3d_resource_state(ResourceState usage);

    private:

        void populateTexture(const std::shared_ptr<Texture>& texture, const TextureDesc& desc) const;

        ComPtr<IDXGIFactory6> m_factory;
        ComPtr<IDXGIAdapter1> m_adapter;
        ComPtr<ID3D12Device> m_device;
        ComPtr<IDStorageFactory> m_dStorage;
        ComPtr<ID3D12InfoQueue1> m_infoQueue;
        D3D12MA::Allocator* m_allocator = nullptr;
        D3D12Context m_context;
        std::array<std::unique_ptr<Queue>, uint32_t(QueueType::Count)> m_queues;
        std::shared_ptr<coro::thread_pool> m_threadPool;
        std::shared_ptr<IStorage> m_storage;
    };

    DevicePtr CreateDevice(const DeviceConfig& config);
}