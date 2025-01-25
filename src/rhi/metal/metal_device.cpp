//
// Created by Loulfy on 21/11/2024.
//

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include "log/log.hpp"
#include "rhi/metal.hpp"

namespace ler::rhi::metal
{
DevicePtr CreateDevice(const DeviceConfig& config)
{
    return std::make_shared<Device>(config);
}

Device::~Device()
{
}

Device::Device(const DeviceConfig& config)
{
    m_device = MTL::CreateSystemDefaultDevice();
    log::info("GPU: {}", m_device->name()->cString(NS::UTF8StringEncoding));
    if (m_device->supportsFamily(MTL::GPUFamilyMetal3))
        log::info("API: Metal 3");

    log::info("RayTracing: {}", m_device->supportsRaytracing());
    log::info("SubgroupSize: {}", m_device->maxThreadsPerThreadgroup().depth);

    m_context.device = m_device;
    m_context.queue = m_device->newCommandQueue();
    m_context.rootSignature = BindlessTable::buildBindlessLayout();

    m_queue = std::make_unique<Queue>(m_context, QueueType::Graphics);

    m_threadPool = std::make_shared<coro::thread_pool>(coro::thread_pool::options{ .thread_count = 8 });
    m_storage = std::make_shared<Storage>(this, m_threadPool);

    fs::path path = sys::ASSETS_DIR / "encoder.metallib";
    NS::Error* mtlError = nil;
    const NS::SharedPtr<MTL::Library> library = NS::TransferPtr(
        m_device->newLibrary(NS::String::string(path.c_str(), NS::StringEncoding::UTF8StringEncoding), &mtlError));

    MTL::Function* func =
        library->newFunction(NS::String::string("cullMeshesAndEncodeCommands", NS::StringEncoding::UTF8StringEncoding));
    const MTL::AutoreleasedComputePipelineReflection* reflection = nullptr;
    m_indirectComputeEncoder = NS::TransferPtr(m_device->newComputePipelineState(func, MTL::PipelineOptionArgumentInfo, reflection, &mtlError));
    m_indirectArgumentEncoder = NS::TransferPtr(func->newArgumentEncoder(4));
    func->release();

    m_context.encoder = m_indirectComputeEncoder.get();
}

BufferPtr Device::createBuffer(uint64_t byteSize, bool staging)
{
    auto buffer = std::make_shared<Buffer>(m_context);

    const MTL::ResourceOptions options = staging ? MTL::ResourceStorageModeShared : MTL::ResourceStorageModePrivate;
    buffer->handle = m_device->newBuffer(byteSize, options);

    return buffer;
}

BufferPtr Device::createBuffer(const BufferDesc& desc)
{
    auto buffer = std::make_shared<Buffer>(m_context);

    MTL::ResourceOptions options;
    if (desc.isStaging || desc.isReadBack)
        options = MTL::ResourceStorageModeShared;
    else if (desc.isManaged)
        options = MTL::ResourceStorageModeManaged;
    else
        options = MTL::ResourceStorageModePrivate;

    MTL::Resource* resource = nullptr;
    buffer->handle = m_device->newBuffer(desc.byteSize, options);
    resource = buffer->handle;

    resource->setLabel(NS::String::string(desc.debugName.c_str(), NS::StringEncoding::UTF8StringEncoding));

    if (const MTL::PixelFormat format = convertFormat(desc.format); format != MTL::PixelFormatInvalid)
    {
        const FormatBlockInfo formatInfo = formatToBlockInfo(desc.format);
        const uint32_t element_num = desc.byteSize / formatInfo.blockSizeByte;
        const MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::textureBufferDescriptor(
            format, element_num, buffer->handle->resourceOptions(), MTL::TextureUsageShaderRead);
        buffer->view = buffer->handle->newTexture(descriptor, 0, desc.byteSize);
    }

    if (desc.isICB)
    {
        const std::string debugName = "[ICB]" + desc.debugName;
        auto* indirectDesc = MTL::IndirectCommandBufferDescriptor::alloc()->init();
        indirectDesc->setCommandTypes(MTL::IndirectCommandTypeDrawIndexed);
        indirectDesc->setMaxVertexBufferBindCount(7);
        indirectDesc->setInheritBuffers(false);
        indirectDesc->setInheritPipelineState(true);

        ICBContainer container;
        container.icb = NS::TransferPtr(m_device->newIndirectCommandBuffer(indirectDesc, 2048u, MTL::ResourceStorageModePrivate));
        container.icb->setLabel(NS::String::string(debugName.c_str(), NS::StringEncoding::UTF8StringEncoding));
        indirectDesc->release();

        container.drawArgs = NS::TransferPtr(m_device->newBuffer(20u * 2048u, MTL::ResourceStorageModePrivate));
        container.drawArgs->setLabel(NS::String::string("DrawArgs", NS::StringEncoding::UTF8StringEncoding));
        container.uniforms = NS::TransferPtr(m_device->newBuffer(2u * 2048u, MTL::ResourceStorageModePrivate));
        container.uniforms->setLabel(NS::String::string("Uniforms", NS::StringEncoding::UTF8StringEncoding));
        container.topLevel = NS::TransferPtr(m_device->newBuffer(132u * 2048u, MTL::ResourceStorageModePrivate));
        container.topLevel->setLabel(NS::String::string("TopLevel", NS::StringEncoding::UTF8StringEncoding));

        container.bindingArgs = NS::TransferPtr(m_device->newBuffer(m_indirectArgumentEncoder->encodedLength(), MTL::ResourceStorageModeShared));
        m_indirectArgumentEncoder->setArgumentBuffer(container.bindingArgs.get(), 0);
        m_indirectArgumentEncoder->setIndirectCommandBuffer(container.icb.get(), 0);
        m_indirectArgumentEncoder->setBuffer(container.uniforms.get(), 0, 1);
        m_indirectArgumentEncoder->setBuffer(container.drawArgs.get(), 0, 2);
        m_indirectArgumentEncoder->setBuffer(container.topLevel.get(), 0, 3);

        buffer->container = std::move(container);
    }

    return buffer;
}

BufferPtr Device::createHostBuffer(uint64_t byteSize)
{
    return createBuffer(byteSize, true);
}

void Buffer::uploadFromMemory(const void* src, uint64_t byteSize) const
{
    if (staging() && sizeBytes() >= byteSize)
        std::memcpy(handle->contents(), src, byteSize);
    else
        log::error("Failed to upload to buffer");
}

void Buffer::getUint(uint32_t* ptr) const
{
    if (staging())
    {
        const auto data = static_cast<uint32_t*>(handle->contents());
        *ptr = data[1];
    }
    else
        log::error("Failed to upload to buffer");
}
} // namespace ler::rhi::metal