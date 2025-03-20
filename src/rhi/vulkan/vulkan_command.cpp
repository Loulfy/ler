//
// Created by Loulfy on 05/12/2023.
//

#include "rhi/vulkan.hpp"

namespace ler::rhi::vulkan
{
CommandPtr Device::createCommand(QueueType type)
{
    CommandPtr cmd = m_queues[static_cast<int>(type)]->getOrCreateCommandBuffer();
    cmd->reset();
    return cmd;
}

void Device::submitCommand(CommandPtr& command)
{
    Queue::CommandPtr cmd = std::static_pointer_cast<Command>(command);
    Queue* queue = m_queues[static_cast<int>(cmd->queueType)].get();
    const std::span bundle{ &cmd, 1 };
    queue->submit(bundle);
}

void Device::submitOneShot(const CommandPtr& command)
{
    m_queues[static_cast<int>(command->queueType)]->submitAndWait(command);
}

void Device::beginFrame(uint32_t frameIndex)
{
    m_context.frameIndex = frameIndex;
    m_context.scratchBufferPool[frameIndex]->reset();
}

void Device::runGarbageCollection()
{
    for (std::unique_ptr<Queue>& queue : m_queues)
    {
        if (queue)
        {
            queue->retireCommandBuffers();
        }
    }

    m_storage->update();
}

Queue::Queue(const VulkanContext& context, QueueType queueID) : rhi::Queue(queueID), m_context(context)
{
    if (queueID == QueueType::Graphics)
        m_queueFamilyIndex = m_context.graphicsQueueFamily;
    if (queueID == QueueType::Transfer)
        m_queueFamilyIndex = m_context.transferQueueFamily;
    m_queue = m_context.device.getQueue(m_queueFamilyIndex, 0);

    constexpr auto semaphoreTypeInfo = vk::SemaphoreTypeCreateInfo().setSemaphoreType(vk::SemaphoreType::eTimeline);
    auto semaphoreInfo = vk::SemaphoreCreateInfo().setPNext(&semaphoreTypeInfo);

    trackingSemaphore = context.device.createSemaphoreUnique(semaphoreInfo);

    if (!m_context.debug)
        return;

    vk::DebugUtilsObjectNameInfoEXT nameInfo;
    nameInfo.setObjectType(vk::ObjectType::eQueue);
    auto raw = static_cast<VkQueue>(m_queue);
    nameInfo.setObjectHandle(reinterpret_cast<uint64_t>(raw));
    if (queueID == QueueType::Graphics)
        nameInfo.setPObjectName("GraphicsQueue");
    if (queueID == QueueType::Transfer)
        nameInfo.setPObjectName("TransferQueue");
    m_context.device.setDebugUtilsObjectNameEXT(nameInfo);
}

CommandPtr Queue::createCommandBuffer()
{
    auto ret = std::make_shared<Command>(m_context);
    ret->queueType = m_queueType;

    auto cmdPoolInfo = vk::CommandPoolCreateInfo();
    cmdPoolInfo.setQueueFamilyIndex(m_queueFamilyIndex);
    cmdPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                         vk::CommandPoolCreateFlagBits::eTransient);

    vk::Result res = m_context.device.createCommandPool(&cmdPoolInfo, nullptr, &ret->cmdPool);
    assert(res == vk::Result::eSuccess);

    // allocate command buffer
    auto allocInfo = vk::CommandBufferAllocateInfo();
    allocInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    allocInfo.setCommandPool(ret->cmdPool);
    allocInfo.setCommandBufferCount(1);

    res = m_context.device.allocateCommandBuffers(&allocInfo, &ret->cmdBuf);
    assert(res == vk::Result::eSuccess);

    return ret;
}

uint64_t Queue::updateLastFinishedID()
{
    m_lastFinishedID = m_context.device.getSemaphoreCounterValue(trackingSemaphore.get());
    return m_lastFinishedID;
}

uint64_t Queue::submit(const std::span<CommandPtr>& ppCmd)
{
    std::vector<vk::PipelineStageFlags> waitStageArray(m_waitSemaphores.size());
    std::vector<vk::CommandBuffer> commandBuffers(ppCmd.size());

    for (size_t i = 0; i < m_waitSemaphores.size(); i++)
    {
        waitStageArray[i] = vk::PipelineStageFlagBits::eTopOfPipe;
    }

    m_lastSubmittedID++;

    for (size_t i = 0; i < ppCmd.size(); i++)
    {
        const CommandPtr& commandBuffer = ppCmd[i];

        // It's time!
        commandBuffer->cmdBuf.end();

        commandBuffers[i] = commandBuffer->cmdBuf;
        commandBuffer->submissionID = m_lastSubmittedID;
        m_commandBuffersInFlight.push_back(commandBuffer);
    }

    m_signalSemaphores.push_back(trackingSemaphore.get());
    m_signalSemaphoreValues.push_back(m_lastSubmittedID);

    auto timelineSemaphoreInfo =
        vk::TimelineSemaphoreSubmitInfo()
            .setSignalSemaphoreValueCount(static_cast<uint32_t>(m_signalSemaphoreValues.size()))
            .setPSignalSemaphoreValues(m_signalSemaphoreValues.data());

    if (!m_waitSemaphoreValues.empty())
    {
        timelineSemaphoreInfo.setWaitSemaphoreValueCount(static_cast<uint32_t>(m_waitSemaphoreValues.size()));
        timelineSemaphoreInfo.setPWaitSemaphoreValues(m_waitSemaphoreValues.data());
    }

    const auto submitInfo = vk::SubmitInfo()
                                .setPNext(&timelineSemaphoreInfo)
                                .setCommandBufferCount(static_cast<uint32_t>(ppCmd.size()))
                                .setPCommandBuffers(commandBuffers.data())
                                .setWaitSemaphoreCount(static_cast<uint32_t>(m_waitSemaphores.size()))
                                .setPWaitSemaphores(m_waitSemaphores.data())
                                .setPWaitDstStageMask(waitStageArray.data())
                                .setSignalSemaphoreCount(static_cast<uint32_t>(m_signalSemaphores.size()))
                                .setPSignalSemaphores(m_signalSemaphores.data());

    m_queue.submit(submitInfo);

    m_waitSemaphores.clear();
    m_waitSemaphoreValues.clear();
    m_signalSemaphores.clear();
    m_signalSemaphoreValues.clear();

    return m_lastSubmittedID;
}

void Queue::submitAndWait(const rhi::CommandPtr& command)
{
    auto* nativeCmd = checked_cast<Command*>(command.get());
    nativeCmd->cmdBuf.end();
    vk::UniqueFence fence = m_context.device.createFenceUnique({});

    {
        std::lock_guard lock(m_mutexSend);
        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(nativeCmd->cmdBuf);
        m_queue.submit(submitInfo, fence.get());
    }

    const vk::Result res = m_context.device.waitForFences(fence.get(), true, std::numeric_limits<uint64_t>::max());
    assert(res == vk::Result::eSuccess);
}

vk::AccessFlags2 util_to_vk_access_flags(ResourceState state)
{
    vk::AccessFlags2 ret = {};
    if (state & ResourceState::CopySrc)
    {
        ret |= vk::AccessFlagBits2::eTransferRead;
    }
    if (state & ResourceState::CopyDest)
    {
        ret |= vk::AccessFlagBits2::eTransferWrite;
    }
    if (state & ResourceState::ConstantBuffer)
    {
        ret |= vk::AccessFlagBits2::eUniformRead | vk::AccessFlagBits2::eVertexAttributeRead;
    }
    if (state & ResourceState::IndexBuffer)
    {
        ret |= vk::AccessFlagBits2::eIndexRead;
    }
    if (state & ResourceState::UnorderedAccess)
    {
        ret |= vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
    }
    if (state & ResourceState::Indirect)
    {
        ret |= vk::AccessFlagBits2::eIndirectCommandRead;
    }
    if (state & ResourceState::RenderTarget)
    {
        ret |= vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
    }
    if (state & ResourceState::DepthWrite)
    {
        ret |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
    }
    if (state & ResourceState::ShaderResource)
    {
        ret |= vk::AccessFlagBits2::eShaderRead;
    }
    if (state & ResourceState::Present)
    {
        ret |= vk::AccessFlagBits2::eMemoryRead;
    }
    if (state & ResourceState::ShadingRateSrc)
    {
        ret |= vk::AccessFlagBits2::eFragmentShadingRateAttachmentReadKHR;
    }
    if (state & ResourceState::Raytracing)
    {
        ret |= vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
    }
    return ret;
}

vk::ImageLayout util_to_vk_image_layout(ResourceState usage)
{
    if (usage & ResourceState::CopySrc)
        return vk::ImageLayout::eTransferSrcOptimal;

    if (usage & ResourceState::CopyDest)
        return vk::ImageLayout::eTransferDstOptimal;

    if (usage & ResourceState::RenderTarget)
        return vk::ImageLayout::eAttachmentOptimal;

    if (usage & ResourceState::DepthWrite)
        return vk::ImageLayout::eAttachmentOptimal;

    if (usage & ResourceState::DepthRead)
        return vk::ImageLayout::eReadOnlyOptimal;

    if (usage & ResourceState::UnorderedAccess)
        return vk::ImageLayout::eGeneral;

    if (usage & ResourceState::ShaderResource)
        return vk::ImageLayout::eShaderReadOnlyOptimal;

    if (usage & ResourceState::Present)
        return vk::ImageLayout::ePresentSrcKHR;

    if (usage == ResourceState::Common)
        return vk::ImageLayout::eGeneral;

    if (usage == ResourceState::ShadingRateSrc)
        return vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR;

    return vk::ImageLayout::eUndefined;
}

vk::PipelineStageFlags2 util_determine_pipeline_stage_flags2(vk::AccessFlags2 _access_flags, QueueType queue_type)
{
    const auto access_flags = static_cast<VkAccessFlags2>(_access_flags);
    vk::PipelineStageFlags2 flags = {};
    switch (queue_type)
    {
    default:
    case QueueType::Graphics: {
        if ((access_flags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0)
            flags |= vk::PipelineStageFlagBits2::eVertexInput;

        if ((access_flags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0)
        {
            flags |= vk::PipelineStageFlagBits2::eVertexShader;
            flags |= vk::PipelineStageFlagBits2::eFragmentShader;
            flags |= vk::PipelineStageFlagBits2::eComputeShader;
            flags |= vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        }

        if ((access_flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0)
            flags |= vk::PipelineStageFlagBits2::eFragmentShader;

        if ((access_flags &
             (VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR)) != 0)
            flags |= vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;

        if ((access_flags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
            flags |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;

        if ((access_flags & VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR) != 0)
            flags |= vk::PipelineStageFlagBits2::eFragmentShadingRateAttachmentKHR;

        if ((access_flags &
             (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
            flags |= vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;

        break;
    }
    case QueueType::Compute: {
        if ((access_flags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0 ||
            (access_flags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0 ||
            (access_flags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0 ||
            (access_flags &
             (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
            return vk::PipelineStageFlagBits2::eAllCommands;

        if ((access_flags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0)
            flags |= vk::PipelineStageFlagBits2::eComputeShader;

        break;
    }
    case QueueType::Transfer:
        return vk::PipelineStageFlagBits2::eAllCommands;
    }

    // Compatible with both compute and graphics queues
    if ((access_flags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT) != 0)
        flags |= vk::PipelineStageFlagBits2::eDrawIndirect;

    if ((access_flags & (VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)) != 0)
        flags |= vk::PipelineStageFlagBits2::eTransfer;

    if ((access_flags & (VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT)) != 0)
        flags |= vk::PipelineStageFlagBits2::eHost;

    if (flags == vk::PipelineStageFlags2())
        flags = vk::PipelineStageFlagBits2::eTopOfPipe;

    return flags;
}

Command::~Command()
{
    m_context.device.destroyCommandPool(cmdPool);
}

void Command::reset()
{
    cmdBuf.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
}

void Command::addImageBarrier(const TexturePtr& texture, ResourceState new_state) const
{
    const auto* image = checked_cast<Texture*>(texture.get());
    ResourceState old_state = texture->state;
    vk::ImageMemoryBarrier2KHR barrier;
    barrier.srcAccessMask = util_to_vk_access_flags(old_state);
    barrier.srcStageMask = util_determine_pipeline_stage_flags2(barrier.srcAccessMask, queueType);
    barrier.dstAccessMask = util_to_vk_access_flags(new_state);
    barrier.dstStageMask = util_determine_pipeline_stage_flags2(barrier.dstAccessMask, queueType);
    barrier.oldLayout = util_to_vk_image_layout(old_state);
    barrier.newLayout = util_to_vk_image_layout(new_state);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setImage(image->handle);
    const vk::ImageAspectFlags aspect = Device::guessImageAspectFlags(image->info.format, false);
    barrier.setSubresourceRange(
        vk::ImageSubresourceRange(aspect, 0, VK_REMAINING_MIP_LEVELS, 0, image->info.arrayLayers));

    texture->state = new_state;
    vk::DependencyInfoKHR dependency_info;
    dependency_info.imageMemoryBarrierCount = 1;
    dependency_info.pImageMemoryBarriers = &barrier;

    /*log::debug("[ImageBarrier: {}] srcStage: {}, dstStage {}, oldLayout: {}, newLayout: {}", native->name,
       vk::to_string(barrier.srcStageMask), vk::to_string(barrier.dstStageMask),
       vk::to_string(barrier.oldLayout), vk::to_string(barrier.newLayout));*/
    cmdBuf.pipelineBarrier2(dependency_info);
}

void Command::addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const
{
    const auto* buf = checked_cast<Buffer*>(buffer.get());
    ResourceState old_state = buffer->state;
    vk::BufferMemoryBarrier2KHR barrier;
    constexpr QueueType type = QueueType::Graphics;
    barrier.srcAccessMask = util_to_vk_access_flags(old_state);
    barrier.srcStageMask = util_determine_pipeline_stage_flags2(barrier.srcAccessMask, type);
    barrier.dstAccessMask = util_to_vk_access_flags(new_state);
    barrier.dstStageMask = util_determine_pipeline_stage_flags2(barrier.dstAccessMask, type);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.buffer = buf->handle;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    buffer->state = new_state;
    vk::DependencyInfoKHR dependency_info;
    dependency_info.bufferMemoryBarrierCount = 1;
    dependency_info.pBufferMemoryBarriers = &barrier;

    cmdBuf.pipelineBarrier2(dependency_info);
    /*log::debug("[BufferBarrier: {}] srcStage: {}, dstStage {}, srcMask: {}, dstMask: {}", buf->name,
               vk::to_string(barrier.srcStageMask), vk::to_string(barrier.dstStageMask),
               vk::to_string(barrier.srcAccessMask), vk::to_string(barrier.dstAccessMask));*/
}

void Command::clearColorImage(const TexturePtr& texture, const std::array<float, 4>& color) const
{
    const auto* image = checked_cast<Texture*>(texture.get());
    constexpr vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    cmdBuf.clearColorImage(image->handle, vk::ImageLayout::eGeneral, color, range);
}

void Command::copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub,
                                  const unsigned char* pSrcData) const
{
    const auto* image = checked_cast<Texture*>(texture.get());
    const auto* staging = checked_cast<Buffer*>(buffer.get());

    if (pSrcData != nullptr)
        buffer->uploadFromMemory(pSrcData, staging->sizeInBytes());

    // prepare texture to transfer layout!
    addImageBarrier(texture, CopyDest);
    // Copy buffer to texture
    vk::BufferImageCopy copyRegion(sub.offset, 0, 0);
    copyRegion.imageExtent.depth = sub.depth;
    copyRegion.imageExtent.width = sub.width;
    copyRegion.imageExtent.height = sub.height;
    copyRegion.bufferRowLength = sub.rowPitch;
    copyRegion.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, sub.index, 0, 1);
    cmdBuf.copyBufferToImage(staging->handle, image->handle, vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);
    // prepare texture to color layout
    addImageBarrier(texture, ShaderResource);
}

void Command::copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t byteSize, uint64_t dstOffset)
{
    const auto* buffSrc = checked_cast<Buffer*>(src.get());
    const auto* buffDst = checked_cast<Buffer*>(dst.get());

    const vk::BufferCopy copyRegion(0u, dstOffset, byteSize);
    cmdBuf.copyBuffer(buffSrc->handle, buffDst->handle, 1, &copyRegion);
}

void Command::syncBuffer(const BufferPtr& dst, const void* src, uint64_t byteSize)
{
    assert(queueType == QueueType::Graphics);
    const std::unique_ptr<ScratchBuffer>& scratchBuffer = m_context.scratchBufferPool[m_context.frameIndex];
    const auto* buffSrc = checked_cast<Buffer*>(scratchBuffer->getBuffer().get());
    const auto* buffDst = checked_cast<Buffer*>(dst.get());
    uint64_t srcOffset = scratchBuffer->allocate(byteSize);
    if (buffSrc->staging() && buffSrc->sizeInBytes() >= byteSize)
    {
        auto* pCursor = static_cast<std::byte*>(buffSrc->hostInfo.pMappedData);
        memcpy(pCursor + srcOffset, src, byteSize);

        addBufferBarrier(dst, CopyDest);
        const vk::BufferCopy copyRegion(srcOffset, 0u, byteSize);
        cmdBuf.copyBuffer(buffSrc->handle, buffDst->handle, 1, &copyRegion);
    }
    else
        log::error("Failed to upload to Scratch Buffer");
}

void Command::fillBuffer(const BufferPtr& dst, uint32_t value) const
{
    const auto* buff = checked_cast<Buffer*>(dst.get());
    cmdBuf.fillBuffer(buff->handle, 0, VK_WHOLE_SIZE, value);
}

void Command::bindIndexBuffer(const BufferPtr& indexBuffer)
{
    constexpr static vk::DeviceSize offset = 0;
    const auto* buff = checked_cast<Buffer*>(indexBuffer.get());
    cmdBuf.bindIndexBuffer(buff->handle, offset, vk::IndexType::eUint32);
}

void Command::bindVertexBuffers(uint32_t slot, const ler::rhi::BufferPtr& vertexBuffer)
{
    constexpr static vk::DeviceSize offset = 0;
    const auto* buff = checked_cast<Buffer*>(vertexBuffer.get());
    cmdBuf.bindVertexBuffers(slot, 1, &buff->handle, &offset);
}

void Command::drawPrimitives(uint32_t vertexCount) const
{
    cmdBuf.draw(vertexCount, 1, 0, 0);
}

void Command::drawIndexedPrimitives(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex,
                                    uint32_t instanceId) const
{
    cmdBuf.drawIndexed(indexCount, 1, firstIndex, firstVertex, instanceId);
}

void Command::drawIndirectIndexedPrimitives(const rhi::PipelinePtr&, const BufferPtr& commands, const BufferPtr& count,
                                            uint32_t maxDrawCount, uint32_t stride)
{
    constexpr static vk::DeviceSize offset = 0;
    const auto* drawsBuff = checked_cast<Buffer*>(commands.get());
    const auto* countBuff = checked_cast<Buffer*>(count.get());
    cmdBuf.drawIndexedIndirectCount(drawsBuff->handle, offset, countBuff->handle, sizeof(uint32_t), maxDrawCount,
                                    stride);
}

void Command::encodeIndirectIndexedPrimitives(const EncodeIndirectIndexedDrawDesc& desc)
{
    // Compatibility with METAL, not needed!
}

void Command::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    cmdBuf.dispatch(x, y, z);
}

vk::Viewport createViewport(const vk::Extent2D& extent)
{
    return { 0.f, 0.f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1.0f };
}

void Command::bindPipeline(const rhi::PipelinePtr& pipeline, uint32_t descriptorHandle) const
{
    auto* native = checked_cast<BasePipeline*>(pipeline.get());
    cmdBuf.bindPipeline(native->bindPoint, native->handle.get());
    cmdBuf.bindDescriptorSets(native->bindPoint, native->pipelineLayout.get(), 0,
                              native->getDescriptorSet(descriptorHandle), nullptr);
}

void Command::bindPipeline(const rhi::PipelinePtr& pipeline, const BindlessTablePtr& table,
                           const BufferPtr& constantBuffer)
{
    auto* native = checked_cast<BasePipeline*>(pipeline.get());
    auto* constant = checked_cast<Buffer*>(constantBuffer.get());
    cmdBuf.bindPipeline(native->bindPoint, native->handle.get());

    auto* bindless = checked_cast<BindlessTable*>(table.get());

    vk::DescriptorBufferBindingInfoEXT bindingInfo;
    bindingInfo.setAddress(bindless->bufferDescriptorGPUAddress());
    bindingInfo.setUsage(vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT |
                         vk::BufferUsageFlagBits::eSamplerDescriptorBufferEXT);
    cmdBuf.bindDescriptorBuffersEXT(bindingInfo);

    constexpr std::array<uint32_t, 2> bufferId = { 0 };
    std::array<vk::DeviceSize, 2> offset = { 0 };
    if(constant->cbvIndex == UINT32_MAX)
        constant->cbvIndex = bindless->createCBV(constant);
    offset[1] = bindless->getOffsetFromIndex(constant->cbvIndex);
    cmdBuf.setDescriptorBufferOffsetsEXT(native->bindPoint, native->pipelineLayout.get(), 0, 2, bufferId.data(), offset.data());
}

static constexpr vk::ShaderStageFlags convertShaderStage(ShaderType stage)
{
    switch (stage)
    {
    case ShaderType::Pixel:
        return vk::ShaderStageFlagBits::eFragment;
    case ShaderType::Vertex:
        return vk::ShaderStageFlagBits::eVertex;
    case ShaderType::Compute:
        return vk::ShaderStageFlagBits::eCompute;
    default:
        log::exit("ShaderStage not implemented");
        return {};
    }
}

void Command::setConstant(const BufferPtr& buffer, ShaderType stage)
{
    constexpr uint32_t bufferId = 0;
    constexpr vk::DeviceSize offset = 0;
    // cmdBuf.setDescriptorBufferOffsetsEXT(native->bindPoint, native->pipelineLayout.get(), 1, 1, &bufferId, &offset);
}

void Command::pushConstant(const rhi::PipelinePtr& pipeline, ShaderType stage, uint32_t slot, const void* data,
                           uint8_t size)
{
    assert(size <= 128);
    auto* native = checked_cast<BasePipeline*>(pipeline.get());
    cmdBuf.pushConstants(native->pipelineLayout.get(), convertShaderStage(stage), 0, size, data);
}

void Command::beginRendering(const RenderingInfo& renderingInfo)
{
    vk::RenderingInfo renderInfo;
    vk::RenderingAttachmentInfo depthAttachment;
    std::vector<vk::RenderingAttachmentInfo> colorAttachments;

    for (size_t i = 0; i < renderingInfo.colorCount; ++i)
    {
        const Attachment& rColorAttachment = renderingInfo.colors[i];
        assert(rColorAttachment.texture);
        auto& attachment = colorAttachments.emplace_back();
        auto* image = checked_cast<Texture*>(rColorAttachment.texture.get());
        attachment.setImageView(image->view());
        attachment.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal);
        attachment.setLoadOp(static_cast<vk::AttachmentLoadOp>(rColorAttachment.loadOp));
        attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        if (attachment.loadOp == vk::AttachmentLoadOp::eClear)
            attachment.setClearValue(vk::ClearColorValue(Color::White));
    }

    if (renderingInfo.depth.texture)
    {
        auto* image = checked_cast<Texture*>(renderingInfo.depth.texture.get());
        depthAttachment.setImageView(image->view());
        depthAttachment.setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal);
        depthAttachment.setLoadOp(vk::AttachmentLoadOp::eClear);
        depthAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        if (depthAttachment.loadOp == vk::AttachmentLoadOp::eClear)
            depthAttachment.setClearValue(vk::ClearDepthStencilValue(1.0f, 0));
        renderInfo.setPDepthAttachment(&depthAttachment);
    }

    const vk::Extent2D extent{ renderingInfo.viewport.width, renderingInfo.viewport.height };
    const vk::Viewport viewport = createViewport(extent);
    const vk::Rect2D renderArea(vk::Offset2D(0, 0), extent);
    renderInfo.setRenderArea(renderArea);
    renderInfo.setLayerCount(1);
    renderInfo.setColorAttachments(colorAttachments);
    cmdBuf.beginRendering(renderInfo);
    cmdBuf.setScissor(0, 1, &renderArea);
    cmdBuf.setViewport(0, 1, &viewport);
}

void Command::beginRendering(const rhi::PipelinePtr& pipeline, TexturePtr& backBuffer)
{
    auto* image = checked_cast<Texture*>(backBuffer.get());

    vk::RenderingInfo renderInfo;
    vk::RenderingAttachmentInfo depthAttachment;
    std::vector<vk::RenderingAttachmentInfo> colorAttachments;

    auto& attachment = colorAttachments.emplace_back();
    attachment.setImageView(image->view());
    attachment.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal);
    attachment.setLoadOp(vk::AttachmentLoadOp::eClear);
    attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
    if (attachment.loadOp == vk::AttachmentLoadOp::eClear)
        attachment.setClearValue(vk::ClearColorValue(Color::White));

    const vk::Extent2D extent{ image->info.extent.width, image->info.extent.height };
    const vk::Viewport viewport = createViewport(extent);
    const vk::Rect2D renderArea(vk::Offset2D(), extent);
    renderInfo.setRenderArea(renderArea);
    renderInfo.setLayerCount(1);
    renderInfo.setColorAttachments(colorAttachments);
    cmdBuf.beginRendering(renderInfo);
    cmdBuf.setScissor(0, 1, &renderArea);
    cmdBuf.setViewport(0, 1, &viewport);
}

void Command::endRendering() const
{
    cmdBuf.endRendering();
}

void Command::beginDebugEvent(const std::string& label, const std::array<float, 4>& color) const
{
    vk::DebugUtilsLabelEXT markerInfo;
    memcpy(markerInfo.color, color.data(), sizeof(float) * 4);
    markerInfo.pLabelName = label.c_str();
    cmdBuf.beginDebugUtilsLabelEXT(markerInfo);
}

void Command::endDebugEvent() const
{
    cmdBuf.endDebugUtilsLabelEXT();
}
} // namespace ler::rhi::vulkan