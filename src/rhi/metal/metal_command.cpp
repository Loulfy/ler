//
// Created by Loulfy on 22/11/2024.
//

#include "rhi/metal.hpp"

namespace ler::rhi::metal
{
CommandPtr Device::createCommand(QueueType type)
{
    CommandPtr cmd = m_queue->getOrCreateCommandBuffer();
    cmd->reset();
    return cmd;
}

void Device::submitOneShot(const CommandPtr& command)
{
    m_queue->submitAndWait(command);
}

Queue::Queue(const MetalContext& context, QueueType queueID) : rhi::Queue(queueID), m_context(context)
{
    // Describe and create the command queue.
    m_queue = context.device->newCommandQueue();
    m_eventTracking = context.device->newSharedEvent();
}

rhi::CommandPtr Queue::createCommandBuffer()
{
    auto command = std::make_shared<Command>(m_context);
    command->queueType = m_queueType;
    command->cmdBuf = m_queue->commandBuffer();
    return command;
}

uint64_t Queue::updateLastFinishedID()
{
    m_lastFinishedID = m_eventTracking->signaledValue();
    return m_lastFinishedID;
}

uint64_t Queue::submit(const std::span<CommandPtr>& ppCmd)
{
    m_lastSubmittedID++;

    ppCmd.front()->submissionID = m_lastSubmittedID;
    ppCmd.front()->cmdBuf->encodeSignalEvent(m_eventTracking, m_lastSubmittedID);
    ppCmd.front()->cmdBuf->commit();

    return m_lastSubmittedID;
}

void Queue::submitAndWait(const rhi::CommandPtr& command)
{
    auto* native = checked_cast<Command*>(command.get());
    native->cmdBuf->commit();
    native->cmdBuf->waitUntilCompleted();
}

void Command::reset()
{
    cmdBuf = m_context.queue->commandBuffer();
    m_computeCommandEncoder = nullptr;
    m_renderCommandEncoder = nullptr;
}

void Command::beginRendering(const RenderingInfo& renderingInfo)
{
    m_renderPassDescriptor = NS::TransferPtr(MTL::RenderPassDescriptor::alloc()->init());

    for (NS::UInteger i = 0; i < renderingInfo.colorCount; ++i)
    {
        const Attachment& rColorAttachment = renderingInfo.colors[i];
        assert(rColorAttachment.texture);
        MTL::RenderPassColorAttachmentDescriptor* cd = m_renderPassDescriptor->colorAttachments()->object(i);
        const auto* image = checked_cast<Texture*>(rColorAttachment.texture.get());
        cd->setTexture(image->handle);
        cd->setLoadAction(MTL::LoadActionClear);
        cd->setStoreAction(MTL::StoreActionStore);
        if(cd->loadAction() == MTL::LoadActionClear)
            cd->setClearColor(MTL::ClearColor(1, 1, 1, 1));
    }

    if (renderingInfo.depth.texture)
    {
        auto* image = checked_cast<Texture*>(renderingInfo.depth.texture.get());
        MTL::RenderPassDepthAttachmentDescriptor* cd = m_renderPassDescriptor->depthAttachment();
        cd->setTexture(image->handle);
        cd->setLoadAction(MTL::LoadActionClear);
        cd->setStoreAction(MTL::StoreActionStore);
        if(cd->loadAction() == MTL::LoadActionClear)
            cd->setClearDepth(1.0f);
    }

    m_renderCommandEncoder = cmdBuf->renderCommandEncoder(m_renderPassDescriptor.get());

    MTL::Viewport viewport(0, 0, renderingInfo.viewport.width, renderingInfo.viewport.height, 0, 1);
    m_renderCommandEncoder->setViewport(viewport);
    m_renderCommandEncoder->setScissorRect(MTL::ScissorRect(0, 0, renderingInfo.viewport.width, renderingInfo.viewport.height));
}

void Command::endRendering() const
{
    m_renderCommandEncoder->endEncoding();
}

void Command::copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub, const unsigned char* pSrcData) const
{
    auto* image = checked_cast<Texture*>(texture.get());
    auto* staging = checked_cast<Buffer*>(buffer.get());

    if (pSrcData != nullptr)
        buffer->uploadFromMemory(pSrcData, staging->sizeBytes());

    const MTL::Origin origin(0, 0, 0);
    const MTL::Size size(sub.width, sub.height, sub.depth);
    MTL::BlitCommandEncoder* cd = cmdBuf->blitCommandEncoder();
    cd->copyFromBuffer(staging->handle, sub.offset, sub.rowPitch, 0, size, image->handle, 0, sub.index, origin);
    cd->endEncoding();
}

void Command::copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t byteSize, uint64_t dstOffset)
{
    const auto* buffSrc = checked_cast<Buffer*>(src.get());
    const auto* buffDst = checked_cast<Buffer*>(dst.get());

    MTL::BlitCommandEncoder* cd = cmdBuf->blitCommandEncoder();
    cd->copyFromBuffer(buffSrc->handle, 0, buffDst->handle, dstOffset, byteSize);
    cd->endEncoding();
}

void Command::syncBuffer(const BufferPtr& dst, const void* src, uint64_t byteSize)
{
    const auto* buffDst = checked_cast<Buffer*>(dst.get());
    MTL::Buffer* buffer = buffDst->handle;

    assert(buffer->storageMode() == MTL::StorageModeManaged);

    memcpy(buffer->contents(), src, byteSize);
    buffer->didModifyRange(NS::Range(0, byteSize));

    MTL::BlitCommandEncoder* cd = cmdBuf->blitCommandEncoder();
    cd->synchronizeResource(buffer);
    cd->endEncoding();
}

void Command::fillBuffer(const BufferPtr& dst, uint32_t value) const
{
    const auto* buffDst = checked_cast<Buffer*>(dst.get());

    MTL::BlitCommandEncoder* cd = cmdBuf->blitCommandEncoder();
    cd->fillBuffer(buffDst->handle, NS::Range::Make(0, buffDst->sizeBytes()), value);
    cd->endEncoding();
}

void Command::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    m_computeCommandEncoder->dispatchThreadgroups(MTL::Size::Make(x, y, z), MTL::Size::Make(32,1,1));
    m_computeCommandEncoder->memoryBarrier(MTL::BarrierScopeBuffers);
    m_computeCommandEncoder->endEncoding();
    m_computeCommandEncoder = nullptr;
}

static constexpr MTL::ResourceUsage getResourceUsage(ResourceState state)
{
    switch (state)
    {

    case Undefined:
    case RenderTarget:

    case CopySrc:
    case Present:
    case Common:
    case Raytracing:
    case ShadingRateSrc:

    case Indirect:
    case ConstantBuffer:
    case IndexBuffer:
    case DepthRead:
    case PixelShader:
    case ShaderResource:
        return MTL::ResourceUsageRead;
    case DepthWrite:
    case CopyDest:
    case UnorderedAccess:
        return MTL::ResourceUsageWrite;
    }
}

void Command::addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const
{
    const MTL::Resource* resource = checked_cast<Buffer*>(buffer.get())->handle;
    if (m_computeCommandEncoder)
        m_computeCommandEncoder->useResource(resource, getResourceUsage(new_state));
}

void Command::beginDebugEvent(const std::string& label, const std::array<float, 4>& color) const
{
    m_renderCommandEncoder->pushDebugGroup(NS::String::string(label.c_str(), NS::StringEncoding::UTF8StringEncoding));
}

void Command::endDebugEvent() const
{
    m_renderCommandEncoder->popDebugGroup();
}
} // namespace ler::rhi::metal