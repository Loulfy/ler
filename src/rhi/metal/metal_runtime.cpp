//
// Created by Loulfy on 03/01/2025.
//

#include "rhi/metal.hpp"

#define IR_RUNTIME_METALCPP       // enable metal-cpp compatibility mode
#define IR_PRIVATE_IMPLEMENTATION // define only once in an implementation file
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>

namespace ler::rhi::metal
{
BindlessTablePtr Device::createBindlessTable(uint32_t count)
{
    return std::make_shared<BindlessTable>(m_context, count);
}

BindlessTable::BindlessTable(const MetalContext& context, uint32_t count) : m_context(context)
{
    m_resourceDescriptor = NS::TransferPtr(
        context.device->newBuffer(count * sizeof(IRDescriptorTableEntry), MTL::ResourceStorageModeShared));
    m_resourceDescriptor->setLabel(NS::String::string("ResourceDescriptor", NS::StringEncoding::UTF8StringEncoding));
    m_samplerDescriptor = NS::TransferPtr(
        context.device->newBuffer(count * sizeof(IRDescriptorTableEntry), MTL::ResourceStorageModeShared));
    m_samplerDescriptor->setLabel(NS::String::string("SamplerDescriptor", NS::StringEncoding::UTF8StringEncoding));
}

bool BindlessTable::visitTexture(const TexturePtr& texture, uint32_t slot)
{
    auto* image = checked_cast<Texture*>(texture.get());
    auto* descriptorTable = static_cast<IRDescriptorTableEntry*>(m_resourceDescriptor->contents());
    std::span<IRDescriptorTableEntry> descriptorTables(descriptorTable, 128);
    IRDescriptorTableSetTexture(descriptorTable + slot, image->view, 0.f, 0);
    return true;
}

bool BindlessTable::visitBuffer(const BufferPtr& buffer, uint32_t slot)
{
    auto* buff = checked_cast<Buffer*>(buffer.get());
    auto* descriptorTable = static_cast<IRDescriptorTableEntry*>(m_resourceDescriptor->contents());
    if (buff->view == nullptr)
        IRDescriptorTableSetBuffer(descriptorTable + slot, buff->handle->gpuAddress(), 0);
    else
    {
        IRBufferView view = {};
        view.buffer = buff->handle;
        view.bufferSize = buff->sizeBytes();
        view.textureBufferView = buff->view;
        view.typedBuffer = true;
        IRDescriptorTableSetBufferView(descriptorTable + slot, &view);
    }
    return true;
}

void BindlessTable::setSampler(const SamplerPtr& sampler, uint32_t slot)
{
    auto* samp = checked_cast<Sampler*>(sampler.get());
    auto* descriptorTable = static_cast<IRDescriptorTableEntry*>(m_samplerDescriptor->contents());
    std::span<IRDescriptorTableEntry> descriptorTables(descriptorTable, 128);
    IRDescriptorTableSetSampler(descriptorTable + slot, samp->handle.get(), 1.f);
}

IRRootSignature* BindlessTable::buildBindlessLayout()
{
    IRRootParameter1 param[2] = {};
    /*param[0].Constants.RegisterSpace = 0;
    param[0].Constants.ShaderRegister = 0;
    param[0].Constants.Num32BitValues = 32;
    param[0].ParameterType = IRRootParameterType32BitConstants;
    param[0].ShaderVisibility = IRShaderVisibilityAll;*/

    param[0].Descriptor.RegisterSpace = 0;
    param[0].Descriptor.ShaderRegister = 0;
    param[0].ParameterType = IRRootParameterTypeCBV;
    param[0].ShaderVisibility = IRShaderVisibilityAll;

    param[1].Constants.RegisterSpace = 0;
    param[1].Constants.ShaderRegister = 1;
    param[1].Constants.Num32BitValues = 1;
    param[1].ParameterType = IRRootParameterType32BitConstants;
    param[1].ShaderVisibility = IRShaderVisibilityVertex;

    IRVersionedRootSignatureDescriptor rootSigDesc = {};
    rootSigDesc.version = IRRootSignatureVersion_1_1;
    rootSigDesc.desc_1_1.Flags = static_cast<IRRootSignatureFlags>(IRRootSignatureFlagCBVSRVUAVHeapDirectlyIndexed |
                                                                   IRRootSignatureFlagSamplerHeapDirectlyIndexed);
    rootSigDesc.desc_1_1.NumParameters = 2;
    rootSigDesc.desc_1_1.pParameters = param;

    IRError* pRootSigError = nullptr;
    IRRootSignature* rootSig = IRRootSignatureCreateFromDescriptor(&rootSigDesc, &pRootSigError);
    assert(rootSig);
    return rootSig;
}

std::vector<MTL::Resource*> BindlessTable::usedResources()
{
    std::vector<MTL::Resource*> res;
    for (uint32_t i = 0; i < getResourceCount(); ++i)
    {
        auto tex = getTexture(i);
        if (tex)
        {
            auto* img = checked_cast<Texture*>(tex.get());
            res.emplace_back(img->view);
        }
        auto buf = getBuffer(i);
        if (buf)
        {
            auto* b = checked_cast<Buffer*>(buf.get());
            res.emplace_back(b->handle);
        }
    }
    return res;
}

void Command::bindPipeline(const rhi::PipelinePtr& pipeline, const BindlessTablePtr& table)
{
    const auto* bindless = checked_cast<BindlessTable*>(table.get());
    const auto* native = checked_cast<BasePipeline*>(pipeline.get());
    if (native->getPipelineType() == BasePipeline::PipelineTypeRender)
    {
        assert(m_renderCommandEncoder);
        m_renderCommandEncoder->setRenderPipelineState(native->renderPipelineState.get());
        if (native->depthStencilState)
            m_renderCommandEncoder->setDepthStencilState(native->depthStencilState.get());

        m_renderCommandEncoder->setTriangleFillMode(native->fillMode);
        m_renderCommandEncoder->setCullMode(MTL::CullModeBack);
        m_renderCommandEncoder->setFrontFacingWinding(MTL::Winding::WindingCounterClockwise);

        m_renderCommandEncoder->setVertexBuffer(bindless->resourceDescriptor(), 0, kIRDescriptorHeapBindPoint);
        m_renderCommandEncoder->setFragmentBuffer(bindless->resourceDescriptor(), 0, kIRDescriptorHeapBindPoint);

        /*m_renderCommandEncoder->setVertexBuffer(heaps[0], 0, kIRDescriptorHeapBindPoint);
        m_renderCommandEncoder->setFragmentBuffer(heaps[0], 0, kIRDescriptorHeapBindPoint);

        m_renderCommandEncoder->setFragmentBuffer(heaps[1], 0, kIRSamplerHeapBindPoint);

        auto res = bindless->usedResources();
        m_renderCommandEncoder->useResources(res.data(), res.size(), MTL::ResourceUsageRead, MTL::RenderStageFragment);
        m_renderCommandEncoder->useResource(heaps[0], MTL::ResourceUsageRead,
                                            MTL::RenderStageVertex | MTL::RenderStageFragment);
        m_renderCommandEncoder->useResource(heaps[1], MTL::ResourceUsageRead,
                                            MTL::RenderStageVertex | MTL::RenderStageFragment);
        m_renderCommandEncoder->useResource(m_context.pushConstantBuffer, MTL::ResourceUsageRead);*/
    }
    else if (native->getPipelineType() == BasePipeline::PipelineTypeCompute)
    {
        m_computeCommandEncoder = cmdBuf->computeCommandEncoder();
        m_computeCommandEncoder->setComputePipelineState(native->computePipelineState.get());
        m_computeCommandEncoder->setBuffer(bindless->resourceDescriptor(), 0, kIRDescriptorHeapBindPoint);

        //auto res = bindless->usedResources();
        //m_computeCommandEncoder->useResources(res.data(), res.size(), MTL::ResourceUsageRead | MTL::ResourceUsageWrite);
    }
}

void Command::setConstant(const BufferPtr& buffer, ShaderType stage)
{
    const auto* constant = checked_cast<Buffer*>(buffer.get());
    const uint64_t cbvAddress = constant->handle->gpuAddress();

    if (stage == ShaderType::Vertex)
        m_renderCommandEncoder->setVertexBytes(&cbvAddress, sizeof(uint64_t), kIRArgumentBufferBindPoint);
    if (stage == ShaderType::Pixel)
        m_renderCommandEncoder->setFragmentBytes(&cbvAddress, sizeof(uint64_t), kIRArgumentBufferBindPoint);
    if (stage == ShaderType::Vertex || stage == ShaderType::Pixel)
        m_renderCommandEncoder->useResource(constant->handle, MTL::ResourceUsageRead);
    if (stage == ShaderType::Compute)
    {
        m_computeCommandEncoder->setBytes(&cbvAddress, sizeof(uint64_t), kIRArgumentBufferBindPoint);
        m_computeCommandEncoder->useResource(constant->handle, MTL::ResourceUsageRead);
    }
}

void Command::pushConstant(const PipelinePtr& pipeline, ShaderType stage, uint32_t slot, const void* data, uint8_t size)
{
    if (stage == ShaderType::Vertex)
        m_renderCommandEncoder->setVertexBytes(data, size, kIRArgumentBufferBindPoint);
    if (stage == ShaderType::Pixel)
        m_renderCommandEncoder->setFragmentBytes(data, size, kIRArgumentBufferBindPoint);
    if (stage == ShaderType::Compute)
        m_computeCommandEncoder->setBytes(data, size, kIRArgumentBufferBindPoint);

    const NS::UInteger offset = slot * 132;

    /*if (stage == ShaderType::Vertex)
        m_renderCommandEncoder->setVertexBuffer(m_context.pushConstantBuffer, offset, kIRArgumentBufferBindPoint);
    if (stage == ShaderType::Pixel)
        m_renderCommandEncoder->setFragmentBuffer(m_context.pushConstantBuffer, offset, kIRArgumentBufferBindPoint);
    if (stage == ShaderType::Compute)
        m_computeCommandEncoder->setBuffer(m_context.pushConstantBuffer, offset, kIRArgumentBufferBindPoint);*/

    //memcpy(static_cast<std::byte*>(m_context.pushConstantBuffer->contents()) + offset, data, size);
    //m_context.pushConstantBuffer->didModifyRange(NS::Range(offset, size));
}

void Command::drawIndexed(uint32_t vertexCount) const
{
    assert(m_renderCommandEncoder);
    IRRuntimeDrawPrimitives(m_renderCommandEncoder, MTL::PrimitiveTypeTriangleStrip, 0, vertexCount, 1);
}

void Command::drawIndexedInstanced(uint32_t indexCount, uint32_t firstIndex, int32_t firstVertex,
                                   uint32_t firstId) const
{
    assert(m_renderCommandEncoder);
    IRRuntimeDrawIndexedPrimitives(m_renderCommandEncoder, MTL::PrimitiveTypeTriangle, indexCount, MTL::IndexTypeUInt32,
                                   m_indexBuffer, firstIndex * sizeof(uint32_t), 1, firstVertex, firstId);
}

void Command::encodeIndirectIndexed(const EncodeIndirectIndexedDrawDesc& desc)
{
    const auto* drawsBuff = checked_cast<Buffer*>(desc.drawsBuffer.get());
    const auto* countBuff = checked_cast<Buffer*>(desc.countBuffer.get());
    assert(drawsBuff->container.has_value());
    const ICBContainer& container = drawsBuff->container.value();

    const auto* indexBuffer = checked_cast<Buffer*>(desc.indexBuffer.get());

    std::vector<const MTL::Buffer*> resources;
    for (const BufferPtr& buffer : desc.vertexBuffer)
    {
        const auto* buff = checked_cast<Buffer*>(buffer.get());
        if (buff == nullptr)
            continue;
        resources.emplace_back(buff->handle);
    }

    MTL::BlitCommandEncoder* bc = cmdBuf->blitCommandEncoder();
    bc->resetCommandsInBuffer(container.icb.get(), NS::Range(0, desc.maxDrawCount));
    bc->endEncoding();

    const auto* bindless = checked_cast<BindlessTable*>(desc.table.get());
    const auto* constant = checked_cast<Buffer*>(desc.constantBuffer.get());
    const uint64_t cbvAddr = constant->handle->gpuAddress();

    m_computeCommandEncoder = cmdBuf->computeCommandEncoder();
    m_computeCommandEncoder->setComputePipelineState(m_context.encoder);

    m_computeCommandEncoder->setBuffer(drawsBuff->handle, 0, kIRArgumentBufferUniformsBindPoint);
    m_computeCommandEncoder->setBuffer(indexBuffer->handle, 0, kIRArgumentBufferHullDomainBindPoint);
    m_computeCommandEncoder->setBuffer(container.bindingArgs.get(), 0, kIRArgumentBufferDrawArgumentsBindPoint);
    m_computeCommandEncoder->setBytes(&cbvAddr, sizeof(uint64_t), kIRArgumentBufferBindPoint);
    m_computeCommandEncoder->setBuffer(bindless->resourceDescriptor(), 0, kIRDescriptorHeapBindPoint);
    m_computeCommandEncoder->setBuffer(resources[0], 0, kIRVertexBufferBindPoint);

    resources.emplace_back(constant->handle);
    resources.emplace_back(drawsBuff->handle);
    resources.emplace_back(indexBuffer->handle);
    resources.emplace_back(container.bindingArgs.get());
    resources.emplace_back(bindless->resourceDescriptor());

    for (const auto* res : resources)
        m_computeCommandEncoder->useResource(res, MTL::ResourceUsageRead);
    m_computeCommandEncoder->useResource(container.icb.get(), MTL::ResourceUsageWrite);
    m_computeCommandEncoder->useResource(container.topLevel.get(), MTL::ResourceUsageWrite);
    m_computeCommandEncoder->useResource(container.uniforms.get(), MTL::ResourceUsageWrite);
    m_computeCommandEncoder->useResource(container.drawArgs.get(), MTL::ResourceUsageWrite);

    m_computeCommandEncoder->dispatchThreadgroups(countBuff->handle, 4, MTL::Size::Make(1, 1, 1));
    m_computeCommandEncoder->memoryBarrier(MTL::BarrierScopeBuffers);
    m_computeCommandEncoder->endEncoding();
}

void Command::drawIndirectIndexed(const PipelinePtr& pipeline, const BufferPtr& commands, const BufferPtr& count,
                                  uint32_t maxDrawCount, uint32_t stride)
{
    assert(m_renderCommandEncoder);
    const auto* drawsBuff = checked_cast<Buffer*>(commands.get());
    const auto* countBuff = checked_cast<Buffer*>(count.get());
    assert(drawsBuff->container.has_value());
    const ICBContainer& container = drawsBuff->container.value();
    m_renderCommandEncoder->useResource(container.icb.get(), MTL::ResourceUsageRead);
    m_renderCommandEncoder->useResource(container.uniforms.get(), MTL::ResourceUsageRead);
    m_renderCommandEncoder->useResource(container.drawArgs.get(), MTL::ResourceUsageRead);
    m_renderCommandEncoder->useResource(container.topLevel.get(), MTL::ResourceUsageRead);
    m_renderCommandEncoder->executeCommandsInBuffer(container.icb.get(), countBuff->handle, 0);
}

void Command::bindIndexBuffer(const BufferPtr& indexBuffer)
{
    const auto* buff = checked_cast<Buffer*>(indexBuffer.get());
    m_indexBuffer = buff->handle;
}

void Command::bindVertexBuffers(uint32_t slot, const BufferPtr& vertexBuffer)
{
    const auto* buff = checked_cast<Buffer*>(vertexBuffer.get());
    if (m_renderCommandEncoder)
        m_renderCommandEncoder->setVertexBuffer(buff->handle, 0, kIRVertexBufferBindPoint + slot);
}
} // namespace ler::rhi::metal