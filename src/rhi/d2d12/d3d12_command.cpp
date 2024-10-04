//
// Created by loulfy on 05/12/2023.
//

#include "rhi/d3d12.hpp"

namespace ler::rhi::d3d12
{
CommandPtr Device::createCommand(QueueType type)
{
    CommandPtr cmd = m_queues[int(type)]->getOrCreateCommandBuffer();
    return cmd;
}

void Device::submitCommand(CommandPtr& command)
{
    Queue::CommandPtr cmd = std::static_pointer_cast<Command>(command);
    Queue* queue = m_queues[int(cmd->queueType)].get();
    auto test = std::span{ &cmd, 1 };
    queue->submit(test);
}

void Device::submitOneShot(const CommandPtr& command)
{
    m_queues[int(command->queueType)]->submitAndWait(command);
}

void Device::runGarbageCollection()
{
    for (auto& queue : m_queues)
    {
        if (queue)
        {
            queue->retireCommandBuffers();
        }
    }

    m_storage->update();
}

Queue::Queue(const D3D12Context& context, QueueType queueID) : rhi::Queue(queueID), m_context(context)
{
    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
    if (queueID == QueueType::Graphics)
        m_commandType = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (queueID == QueueType::Transfer)
        m_commandType = D3D12_COMMAND_LIST_TYPE_COPY;

    queueDesc.Type = m_commandType;
    m_context.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    m_context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
}

rhi::CommandPtr Queue::createCommandBuffer()
{
    auto command = std::make_shared<Command>();
    command->queueType = m_queueType;

    m_context.device->CreateCommandAllocator(m_commandType,
                                             IID_PPV_ARGS(&command->m_commandAllocator));
    m_context.device->CreateCommandList(0, m_commandType, command->m_commandAllocator.Get(), nullptr,
                                        IID_PPV_ARGS(&command->m_commandList));
    return command;
}

uint64_t Queue::updateLastFinishedID()
{
    m_lastFinishedID = m_fence->GetCompletedValue();
    return m_lastFinishedID;
}

uint64_t Queue::submit(const std::span<CommandPtr>& ppCmd)
{
    std::vector<ID3D12CommandList*> commandLists(ppCmd.size());

    m_lastSubmittedID++;

    for (size_t i = 0; i < ppCmd.size(); i++)
    {
        CommandPtr commandBuffer = ppCmd[i];

        // It's time!
        commandBuffer->m_commandList->Close();

        commandLists[i] = commandBuffer->m_commandList.Get();
        commandBuffer->submissionID = m_lastSubmittedID;
        m_commandBuffersInFlight.push_back(commandBuffer);
    }

    m_commandQueue->ExecuteCommandLists(commandLists.size(), commandLists.data());
    m_commandQueue->Signal(m_fence.Get(), m_lastSubmittedID);

    return m_lastSubmittedID;
}

void Queue::submitAndWait(const rhi::CommandPtr& command)
{
    auto* native = checked_cast<Command*>(command.get());
    native->m_commandList->Close();

    ComPtr<ID3D12Fence> fence;
    m_context.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    {
        std::lock_guard lock(m_mutexSend);
        // Execute the command list.
        ID3D12CommandList* ppCommandLists[] = { native->m_commandList.Get() };
        ID3D12CommandQueue* commandQueue = m_commandQueue.Get();
        commandQueue->ExecuteCommandLists(1, ppCommandLists);
        commandQueue->Signal(fence.Get(), 1);
    }

    fence->SetEventOnCompletion(1, event);
    WaitForSingleObjectEx(event, INFINITE, FALSE);
    CloseHandle(event);
}

D3D12_RESOURCE_STATES Device::util_to_d3d_resource_state(ResourceState usage)
{
    D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON;
    if (usage & ConstantBuffer)
        result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

    if (usage & IndexBuffer)
        result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;

    if (usage & Indirect)
        result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    if (usage & ResourceState::CopySrc)
        result |= D3D12_RESOURCE_STATE_COPY_SOURCE;

    if (usage & ResourceState::CopyDest)
        result |= D3D12_RESOURCE_STATE_COPY_DEST;

    if (usage & ResourceState::RenderTarget)
        result |= D3D12_RESOURCE_STATE_RENDER_TARGET;

    if (usage & ResourceState::DepthWrite)
        result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;

    if (usage & ResourceState::DepthRead)
        result |= D3D12_RESOURCE_STATE_DEPTH_READ;

    if (usage & ResourceState::UnorderedAccess)
        result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    if (usage & ResourceState::ShaderResource)
        result |= D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

    if (usage & ResourceState::Present)
        result |= D3D12_RESOURCE_STATE_PRESENT;

    if (usage == ResourceState::Common)
        result |= D3D12_RESOURCE_STATE_COMMON;

    if (usage == ResourceState::ShadingRateSrc)
        result |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;

    return result;
}

void Command::reset()
{
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), nullptr);
}

void Command::addImageBarrier(const TexturePtr& texture, ResourceState new_state) const
{
    if (texture->state == new_state)
        return;
    ResourceState old_state = texture->state;
    auto image = checked_cast<Texture*>(texture.get());
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(image->handle, Device::util_to_d3d_resource_state(old_state),
                                                        Device::util_to_d3d_resource_state(new_state));

    /*log::debug("[ImageBarrier] srcStage: {}, dstStage {}",
               to_string(texture->state), to_string(new_state));*/
    texture->state = new_state;
    m_commandList->ResourceBarrier(1, &barrier);
}

void Command::addBufferBarrier(const BufferPtr& buffer, ResourceState new_state) const
{
    if (buffer->state == new_state)
        return;
    ResourceState old_state = buffer->state;
    auto buf = checked_cast<Buffer*>(buffer.get());
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(buf->handle, Device::util_to_d3d_resource_state(old_state),
                                                        Device::util_to_d3d_resource_state(new_state));

    buffer->state = new_state;
    m_commandList->ResourceBarrier(1, &barrier);
}

void Command::clearColorImage(const TexturePtr& texture, const std::array<float, 4>& color) const
{
    auto* image = checked_cast<Texture*>(texture.get());
    m_commandList->ClearRenderTargetView(image->rtvDescriptor.getCpuHandle(), color.data(), 0, nullptr);
}

void Command::copyBufferToTexture(const BufferPtr& buffer, const TexturePtr& texture, const Subresource& sub,
                                  const unsigned char* pSrcData) const
{
    auto* image = checked_cast<Texture*>(texture.get());
    auto* staging = checked_cast<Buffer*>(buffer.get());

    // prepare texture to transfer layout!
    addImageBarrier(texture, CopyDest);
    // Copy buffer to texture

    if (pSrcData == nullptr)
    {
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
        layout.Offset = sub.offset;
        layout.Footprint.Depth = sub.depth;
        layout.Footprint.Width = sub.width;
        layout.Footprint.Height = sub.height;
        layout.Footprint.RowPitch = sub.rowPitch;
        layout.Footprint.Format = image->desc.Format;
        CD3DX12_TEXTURE_COPY_LOCATION Dst(image->handle, sub.index);
        CD3DX12_TEXTURE_COPY_LOCATION Src(staging->handle, layout);
        m_commandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
    }
    else
    {
        D3D12_SUBRESOURCE_DATA textureData = {};
        textureData.pData = pSrcData;
        textureData.RowPitch = sub.rowPitch;                        // static_cast<LONG_PTR>(4 * image->desc.Width);
        textureData.SlicePitch = textureData.RowPitch * sub.height; // image->desc.Height;
        UpdateSubresources(m_commandList.Get(), image->handle, staging->handle, 0, sub.index, 1, &textureData);
    }

    // prepare texture to color layout
    //addImageBarrier(texture, ShaderResource);
}

void Command::copyBuffer(const BufferPtr& src, const BufferPtr& dst, uint64_t byteSize, uint64_t dstOffset)
{
    auto* buffSrc = checked_cast<Buffer*>(src.get());
    auto* buffDst = checked_cast<Buffer*>(dst.get());
    m_commandList->CopyBufferRegion(buffDst->handle, dstOffset, buffSrc->handle, 0, byteSize);
}

void Command::fillBuffer(const BufferPtr& dst, uint32_t value) const
{
    auto* buff = checked_cast<Buffer*>(dst.get());
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = buff->clearGpuHandle.getGpuHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = buff->clearCpuHandle.getCpuHandle();
    static std::array<uint32_t,4> clears;
    clears.fill(value);
    m_commandList->ClearUnorderedAccessViewUint(gpuHandle, cpuHandle, buff->handle, clears.data(), 0, nullptr);
}

void Command::bindIndexBuffer(const BufferPtr& indexBuffer)
{
    auto* buff = checked_cast<Buffer*>(indexBuffer.get());
    D3D12_INDEX_BUFFER_VIEW view = {};
    view.SizeInBytes = buff->desc.Width;
    view.Format = DXGI_FORMAT_R32_UINT;
    view.BufferLocation = buff->handle->GetGPUVirtualAddress();
    m_commandList->IASetIndexBuffer(&view);
}

void Command::bindVertexBuffers(uint32_t slot, const BufferPtr& indexBuffer)
{
    auto* buff = checked_cast<Buffer*>(indexBuffer.get());
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.SizeInBytes = buff->desc.Width;
    view.StrideInBytes = 12;
    view.BufferLocation = buff->handle->GetGPUVirtualAddress();
    m_commandList->IASetVertexBuffers(slot, 1, &view);
}

void Command::drawIndexed(uint32_t vertexCount) const
{
    m_commandList->DrawInstanced(vertexCount, 1, 0, 0);
}

void Command::drawIndexedInstanced(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex,
                                   uint32_t firstId) const
{
    m_commandList->DrawIndexedInstanced(indexCount, 1, firstIndex, firstVertex, firstId);
}

void Command::drawIndirectIndexed(const rhi::PipelinePtr& pipeline, const BufferPtr& commands, const BufferPtr& count,
                                  uint32_t maxDrawCount, uint32_t stride) const
{
    auto* drawsBuff = checked_cast<Buffer*>(commands.get());
    auto* countBuff = checked_cast<Buffer*>(count.get());
    constexpr static UINT64 offset = 0;

    ComPtr<ID3D12CommandSignature> signature = checked_cast<Pipeline*>(pipeline.get())->commandSignature.Get();
    m_commandList->ExecuteIndirect(signature.Get(), maxDrawCount, drawsBuff->handle, offset, countBuff->handle, offset);
}

void Command::dispatch(uint32_t x, uint32_t y, uint32_t z) const
{
    m_commandList->Dispatch(x, y, z);
}

void Command::bindPipeline(const PipelinePtr& pipeline, uint32_t descriptorHandle) const
{
    auto* native = checked_cast<Pipeline*>(pipeline.get());
    m_commandList->SetPipelineState(native->pipelineState.Get());

    DescriptorSet& descriptorSet = native->getDescriptorSet(descriptorHandle);
    auto& heaps = descriptorSet.heaps;
    m_commandList->SetDescriptorHeaps(heaps.size(), heaps.data());

    if (native->isGraphics())
    {
        m_commandList->SetGraphicsRootSignature(native->rootSignature.Get());
        m_commandList->IASetPrimitiveTopology(native->topology);
    }
    else
    {
        m_commandList->SetComputeRootSignature(native->rootSignature.Get());
    }

    for (size_t i = 0; i < descriptorSet.tables.size(); ++i)
    {
        DescriptorHeapAllocation& alloc = descriptorSet.tables[i];
        if (!alloc.isNull())
        {
            if (native->isGraphics())
                m_commandList->SetGraphicsRootDescriptorTable(i, alloc.getGpuHandle());
            else
                m_commandList->SetComputeRootDescriptorTable(i, alloc.getGpuHandle());
        }
    }
}

void Command::bindPipeline(const PipelinePtr& pipeline, const BindlessTablePtr& table) const
{
    auto* native = checked_cast<Pipeline*>(pipeline.get());
    m_commandList->SetPipelineState(native->pipelineState.Get());

    auto* bindless = checked_cast<BindlessTable*>(table.get());
    //DescriptorSet& descriptorSet = native->getDescriptorSet(0);

    const std::array<ID3D12DescriptorHeap*,2> heaps = bindless->heaps();
    m_commandList->SetDescriptorHeaps(heaps.size(), heaps.data());

    if (native->isGraphics())
    {
        m_commandList->SetGraphicsRootSignature(native->rootSignature.Get());
        m_commandList->IASetPrimitiveTopology(native->topology);
    }
    else
    {
        m_commandList->SetComputeRootSignature(native->rootSignature.Get());
    }
}

void Command::pushConstant(const PipelinePtr& pipeline, const void* data, uint8_t size) const
{
    m_commandList->SetGraphicsRoot32BitConstants(0u, size/sizeof(uint32_t), data, 0u);
}

void Command::beginRendering(const rhi::PipelinePtr& pipeline, TexturePtr& backBuffer)
{
    D3D12_VIEWPORT m_viewport(0.0f, 0.0f, 1080, 720, 0, 1.0f);
    tagRECT m_scissorRect(0, 0, 1080, 720);
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    auto* image = checked_cast<Texture*>(backBuffer.get());
    // m_commandList->ClearRenderTargetView(image->descriptor, Color::Magenta.data(), 0, nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = image->rtvDescriptor.getCpuHandle();
    m_commandList->OMSetRenderTargets(1, &handle, FALSE, nullptr);
}

void Command::beginRendering(const RenderingInfo& renderingInfo) const
{
    auto color = rhi::Color::White;
    const Extent& viewport = renderingInfo.viewport;
    D3D12_VIEWPORT m_viewport(0.0f, 0.0f, float(viewport.width), float(viewport.height), 0, 1.0f);
    tagRECT m_scissorRect(0, 0, long(viewport.width), long(viewport.height));
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    D3D12_CPU_DESCRIPTOR_HANDLE depth = {};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 8> colors = {};
    for (size_t i = 0; i < renderingInfo.colorCount; ++i)
    {
        const Attachment& rColorAttachment = renderingInfo.colors[i];
        assert(rColorAttachment.texture);
        auto* image = checked_cast<Texture*>(rColorAttachment.texture.get());
        colors[i] = image->rtvDescriptor.getCpuHandle();
        if(rColorAttachment.loadOp == AttachmentLoadOp::Clear)
            m_commandList->ClearRenderTargetView(image->rtvDescriptor.getCpuHandle(), color.data(), 0, nullptr);
    }

    if (renderingInfo.depth.texture)
    {
        auto* image = checked_cast<Texture*>(renderingInfo.depth.texture.get());
        depth = image->rtvDescriptor.getCpuHandle();
        m_commandList->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0.f, 0, nullptr);
    }

    m_commandList->OMSetRenderTargets(renderingInfo.colorCount, colors.data(), FALSE,
                                      depth.ptr == 0u ? nullptr : &depth);
}

void Command::endRendering() const
{
}

void Command::beginDebugEvent(const std::string& label, const std::array<float, 4>& color) const
{
    BYTE r = static_cast<BYTE>(color[0] * 255);
    BYTE g = static_cast<BYTE>(color[1] * 255);
    BYTE b = static_cast<BYTE>(color[2] * 255);
    PIXBeginEvent(m_commandList.Get(), PIX_COLOR(r, g, b), label.c_str());
}

void Command::endDebugEvent() const
{
    PIXEndEvent(m_commandList.Get());
}
} // namespace ler::rhi::d3d12