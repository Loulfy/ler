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
    if(m_device->supportsFamily(MTL::GPUFamilyMetal3))
        log::info("API: Metal 3");

    log::info("RayTracing: {}", m_device->supportsRaytracing());
    log::info("SubgroupSize: {}", m_device->maxThreadsPerThreadgroup().depth);

    m_context.device = m_device;
    m_context.queue = m_device->newCommandQueue();
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
        auto data = static_cast<uint32_t*>(handle->contents());
        *ptr = *data;
    }
    else
        log::error("Failed to upload to buffer");
}
} // namespace ler::rhi::metal