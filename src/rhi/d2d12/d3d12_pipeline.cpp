//
// Created by Loulfy on 02/12/2023.
//

#include "log/log.hpp"
#include "rhi/d3d12.hpp"

namespace ler::rhi::d3d12
{
static constexpr std::string_view SitToString(D3D_SHADER_INPUT_TYPE type)
{
    switch (type)
    {
    case D3D_SIT_CBUFFER:
        return "ConstantBuffer";
    case D3D_SIT_TBUFFER:
    case D3D_SIT_TEXTURE:
        return "Texture";
    case D3D_SIT_SAMPLER:
        return "Sampler";
    case D3D_SIT_STRUCTURED:
        return "ReadOnlyBuffer";
    case D3D_SIT_UAV_RWTYPED:
    case D3D_SIT_UAV_RWSTRUCTURED:
        return "ReadWriteBuffer";
    default:
    case D3D_SIT_BYTEADDRESS:
    case D3D_SIT_UAV_RWBYTEADDRESS:
    case D3D_SIT_UAV_APPEND_STRUCTURED:
    case D3D_SIT_UAV_CONSUME_STRUCTURED:
    case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
        return "Unknown";
    case D3D_SIT_RTACCELERATIONSTRUCTURE:
        return "AccelerationStructure";
    case D3D_SIT_UAV_FEEDBACKTEXTURE:
        return "SamplerFeedback";
    }
}

static constexpr char SitToChar(D3D12_DESCRIPTOR_RANGE_TYPE type)
{
    switch (type)
    {
    default:
    case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
        return 't';
    case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
        return 'u';
    case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
        return 'b';
    case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
        return 's';
    }
}

static constexpr D3D12_DESCRIPTOR_RANGE_TYPE convertDescriptorType(D3D_SHADER_INPUT_TYPE type)
{
    switch (type)
    {
    case D3D_SIT_CBUFFER:
        return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case D3D_SIT_SAMPLER:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    default:
    case D3D_SIT_TBUFFER:
    case D3D_SIT_TEXTURE:
    case D3D_SIT_STRUCTURED:
    case D3D_SIT_BYTEADDRESS:
    case D3D_SIT_RTACCELERATIONSTRUCTURE:
        return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case D3D_SIT_UAV_RWTYPED:
    case D3D_SIT_UAV_RWSTRUCTURED:
    case D3D_SIT_UAV_RWBYTEADDRESS:
    case D3D_SIT_UAV_APPEND_STRUCTURED:
    case D3D_SIT_UAV_CONSUME_STRUCTURED:
    case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
    case D3D_SIT_UAV_FEEDBACKTEXTURE:
        return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    }
}

static constexpr D3D12_PRIMITIVE_TOPOLOGY_TYPE convertPrimitive(PrimitiveType primitive)
{
    switch (primitive)
    {
    case PrimitiveType::PointList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveType::LineList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    default:
    case PrimitiveType::TriangleFan:
    case PrimitiveType::TriangleList:
    case PrimitiveType::TriangleStrip:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveType::TriangleListWithAdjacency:
    case PrimitiveType::TriangleStripWithAdjacency:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    case PrimitiveType::PatchList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }
}

D3D_PRIMITIVE_TOPOLOGY Device::convertTopology(PrimitiveType primitive)
{
    switch (primitive)
    {
    case PrimitiveType::PointList:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveType::LineList:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    default:
    case PrimitiveType::TriangleFan:
    case PrimitiveType::TriangleList:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveType::TriangleStrip:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveType::TriangleListWithAdjacency:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
    case PrimitiveType::TriangleStripWithAdjacency:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
    case PrimitiveType::PatchList:
        return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
    }
}

static void maskToFormat(const D3D12_SIGNATURE_PARAMETER_DESC& inputDesc, D3D12_INPUT_ELEMENT_DESC& inputElement)
{
    switch (inputDesc.Mask)
    {
    case 1:
        if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
            inputElement.Format = DXGI_FORMAT_R32_UINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
            inputElement.Format = DXGI_FORMAT_R32_SINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
            inputElement.Format = DXGI_FORMAT_R32_FLOAT;
        break;
    case 3:
        if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
            inputElement.Format = DXGI_FORMAT_R32G32_UINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
            inputElement.Format = DXGI_FORMAT_R32G32_SINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
            inputElement.Format = DXGI_FORMAT_R32G32_FLOAT;
        break;
    case 7:
        if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
            inputElement.Format = DXGI_FORMAT_R32G32B32_UINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
            inputElement.Format = DXGI_FORMAT_R32G32B32_SINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
            inputElement.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        break;
    case 15:
        if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_UINT32)
            inputElement.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_SINT32)
            inputElement.Format = DXGI_FORMAT_R32G32B32A32_SINT;
        else if (inputDesc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
            inputElement.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        break;
    default:
        break;
    }
}

ShaderPtr Device::createShader(const ShaderModule& shaderModule) const
{
    fs::path path = shaderModule.path;
    path.concat(".cso");

    ShaderPtr shader = std::make_shared<Shader>();
    shader->stage = shaderModule.stage;

    ComPtr<IDxcUtils> utils;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));

    ComPtr<IDxcBlobEncoding> sourceBlob;
    utils->LoadFile(path.c_str(), nullptr, &sourceBlob);
    shader->bytecode = sourceBlob;

    const DxcBuffer reflectionBuffer(sourceBlob->GetBufferPointer(), sourceBlob->GetBufferSize(), 0u);

    ComPtr<ID3D12ShaderReflection> shaderReflection;
    utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&shaderReflection));
    D3D12_SHADER_DESC shaderDesc{};
    shaderReflection->GetDesc(&shaderDesc);

    log::debug("======================================================");
    log::debug("Reflect Shader: {}, Stage: {}", path.stem().string(), to_string(shaderModule.stage));

    UINT64 requiresFlags = shaderReflection->GetRequiresFlags();
    if (requiresFlags & D3D_SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING)
        shader->descriptorHeapIndexing = true;

    if (shaderModule.stage == ShaderType::Vertex)
    {
        shader->inputElementSemanticNames.reserve(shaderDesc.InputParameters);
        shader->inputElementDescs.reserve(shaderDesc.InputParameters);

        for (const uint32_t parameterIndex : std::views::iota(0u, shaderDesc.InputParameters))
        {
            D3D12_SIGNATURE_PARAMETER_DESC signatureParameterDesc{};
            shaderReflection->GetInputParameterDesc(parameterIndex, &signatureParameterDesc);

            // Using the semantic name provided by the signatureParameterDesc directly to the input element desc will
            // cause the SemanticName field to have garbage values. This is because the SemanticName filed is a const
            // wchar_t*. I am using a separate std::vector<std::string> for simplicity.
            shader->inputElementSemanticNames.emplace_back(signatureParameterDesc.SemanticName);

            if (signatureParameterDesc.SystemValueType == D3D_NAME_INSTANCE_ID)
                continue;

            shader->inputElementDescs.emplace_back(D3D12_INPUT_ELEMENT_DESC{
                .SemanticName = shader->inputElementSemanticNames.back().c_str(),
                .SemanticIndex = signatureParameterDesc.SemanticIndex,
                .Format = DXGI_FORMAT_UNKNOWN,
                .InputSlot = UINT(shader->inputElementDescs.size()),
                .AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT,
                .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
                .InstanceDataStepRate = 0u,
            });

            maskToFormat(signatureParameterDesc, shader->inputElementDescs.back());
        }
    }

    D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    for (const uint32_t i : std::views::iota(0u, shaderDesc.BoundResources))
    {
        D3D12_SHADER_INPUT_BIND_DESC bindDesc;
        shaderReflection->GetResourceBindingDesc(i, &bindDesc);
        D3D12_DESCRIPTOR_RANGE_TYPE rangeType = convertDescriptorType(bindDesc.Type);
        const char spaceRegister = SitToChar(rangeType);

        log::debug("space = {}, register = {}{}, count = {:02}, type = {}", bindDesc.Space, spaceRegister,
                   bindDesc.BindPoint, bindDesc.BindCount, SitToString(bindDesc.Type));

        ShaderBindDesc d(rangeType, bindDesc.BindPoint, bindDesc.BindCount, bindDesc.NumSamples, bindDesc.Name);

        if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
        {
            CD3DX12_DESCRIPTOR_RANGE1& range = shader->rangesSampler.emplace_back();
            range.Init(rangeType, bindDesc.BindCount, bindDesc.BindPoint, bindDesc.Space, flags);
        }
        else if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
        {
            ID3D12ShaderReflectionConstantBuffer* shaderReflectionConstantBuffer =
                shaderReflection->GetConstantBufferByIndex(i);
            D3D12_SHADER_BUFFER_DESC constantBufferDesc{};
            shaderReflectionConstantBuffer->GetDesc(&constantBufferDesc);
            d.stride = constantBufferDesc.Size;

            CD3DX12_DESCRIPTOR_RANGE1& range = shader->rangesCbvSrvUav.emplace_back();
            range.Init(rangeType, bindDesc.BindCount, bindDesc.BindPoint, bindDesc.Space);
        }
        else
        {
            CD3DX12_DESCRIPTOR_RANGE1& range = shader->rangesCbvSrvUav.emplace_back();
            if (bindDesc.BindCount == 0)
                range.Init(rangeType, 26, bindDesc.BindPoint, bindDesc.Space, flags);
            else
                range.Init(rangeType, bindDesc.BindCount, bindDesc.BindPoint, bindDesc.Space, flags);
        }

        shader->bindingMap.insert({ bindDesc.BindPoint, d });
    }

    return shader;
}

void Pipeline::merge(int type, DescriptorRanges& ranges)
{
    DescriptorRanges& dest = m_ranges[type];
    dest.insert(dest.end(), std::make_move_iterator(ranges.begin()), std::make_move_iterator(ranges.end()));
}

static bool compareDescriptorRange(const CD3DX12_DESCRIPTOR_RANGE1& a, const CD3DX12_DESCRIPTOR_RANGE1& b)
{
    return a.BaseShaderRegister < b.BaseShaderRegister;
}

void Pipeline::initRootSignature(bool indirect)
{
    std::vector<CD3DX12_ROOT_PARAMETER1> rootParameters;
    if (m_descriptorHeapIndexing)
    {
        for (auto& [set, binding] : bindingMap)
        {
            CD3DX12_ROOT_PARAMETER1& rootParam = rootParameters.emplace_back();
            if (binding.type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
            {
                rootParam.InitAsConstants(binding.stride / sizeof(uint32_t), binding.bindPoint);
            }
        }
    }
    else
    {
        for (DescriptorRanges& ranges : m_ranges)
        {
            if (!ranges.empty())
            {
                std::sort(ranges.begin(), ranges.end(), compareDescriptorRange);
                auto& rootParam = rootParameters.emplace_back();
                rootParam.InitAsDescriptorTable(ranges.size(), ranges.data());
            }
        }
    }

    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned
    // will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_context.device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    D3D12_ROOT_SIGNATURE_FLAGS Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                       D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    rootSignatureDesc.Init_1_1(rootParameters.size(), rootParameters.data(), 0, nullptr, Flags);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
    if (error && error->GetBufferSize() > 0)
    {
        LPVOID errorMessage = error->GetBufferPointer();
        log::error(std::string(static_cast<LPCSTR>(errorMessage)));
    }
    m_context.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                          IID_PPV_ARGS(&rootSignature));

    if (m_graphics && indirect)
    {
        D3D12_INDIRECT_ARGUMENT_DESC argumentDesc[2] = {};
        argumentDesc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        argumentDesc[0].Constant.RootParameterIndex = 1;
        argumentDesc[0].Constant.Num32BitValuesToSet = 1;
        argumentDesc[0].Constant.DestOffsetIn32BitValues = 0;
        argumentDesc[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
        commandSignatureDesc.pArgumentDescs = argumentDesc;
        commandSignatureDesc.NumArgumentDescs = 2;
        commandSignatureDesc.ByteStride = 28;
        m_context.device->CreateCommandSignature(&commandSignatureDesc, rootSignature.Get(),
                                                 IID_PPV_ARGS(&commandSignature));
    }
}

void Device::fillGraphicsPsoDesc(const PipelineDesc& desc, const std::span<ShaderPtr>& shaders,
                                 D3D12_GRAPHICS_PIPELINE_STATE_DESC& psoDesc)
{
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = true;
    if (desc.fillMode == RasterFillMode::Solid)
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    else
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = desc.writeDepth;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable = false;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = convertPrimitive(desc.topology);
    psoDesc.NumRenderTargets = desc.colorAttach.size();
    for (size_t i = 0; i < desc.colorAttach.size(); ++i)
        psoDesc.RTVFormats[i] = convertFormatRtv(desc.colorAttach[i]);
    psoDesc.SampleDesc.Count = desc.sampleCount;
    psoDesc.DSVFormat = convertFormatRtv(desc.depthAttach);

    ID3DBlob* bytecode;
    for (const ShaderPtr& shader : shaders)
    {
        bytecode = reinterpret_cast<ID3DBlob*>(shader->bytecode.Get());
        if (shader->stage == ShaderType::Vertex)
        {
            psoDesc.VS = CD3DX12_SHADER_BYTECODE(bytecode);
            psoDesc.InputLayout = { shader->inputElementDescs.data(), uint32_t(shader->inputElementDescs.size()) };
        }
        else if (shader->stage == ShaderType::Pixel)
            psoDesc.PS = CD3DX12_SHADER_BYTECODE(bytecode);
    }
}

void Device::fillComputePsoDesc(const std::span<ShaderPtr>& shaders, D3D12_COMPUTE_PIPELINE_STATE_DESC& psoDesc)
{
    ID3DBlob* bytecode;
    for (const ShaderPtr& shader : shaders)
    {
        bytecode = reinterpret_cast<ID3DBlob*>(shader->bytecode.Get());
        if (shader->stage == ShaderType::Compute)
            psoDesc.CS = CD3DX12_SHADER_BYTECODE(bytecode);
    }
}

PipelinePtr Device::createGraphicsPipeline(const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc)
{
    std::vector<ShaderPtr> shaders;
    for (const ShaderModule& shaderModule : shaderModules)
        shaders.emplace_back(createShader(shaderModule));

    auto pipeline = std::make_shared<Pipeline>(m_context);

    for (const ShaderPtr& shader : shaders)
    {
        pipeline->merge(0, shader->rangesCbvSrvUav);
        pipeline->merge(1, shader->rangesSampler);
        pipeline->bindingMap.merge(shader->bindingMap);
        if (shader->descriptorHeapIndexing)
            pipeline->m_descriptorHeapIndexing = true;
    }

    pipeline->initRootSignature(desc.indirectDraw);
    pipeline->topology = convertTopology(desc.topology);

    // Describe and create the graphics pipeline state objects (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pipeline->rootSignature.Get();
    fillGraphicsPsoDesc(desc, shaders, psoDesc);
    m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));

    return pipeline;
}

rhi::PipelinePtr Device::createComputePipeline(const ShaderModule& shaderModule)
{
    auto pipeline = std::make_shared<Pipeline>(m_context);

    ShaderPtr shader = createShader(shaderModule);
    if (shader->descriptorHeapIndexing)
        pipeline->m_descriptorHeapIndexing = true;

    pipeline->merge(0, shader->rangesCbvSrvUav);
    pipeline->bindingMap.merge(shader->bindingMap);
    pipeline->initRootSignature();
    pipeline->m_graphics = false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = pipeline->rootSignature.Get();
    fillComputePsoDesc(std::span(&shader, 1), psoDesc);

    m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));

    return pipeline;
}

void Pipeline::createDescriptorSet(uint32_t set)
{
    uint32_t count;
    DescriptorSet& root = m_descriptors.emplace_back();

    for (size_t i = 0; i < m_ranges.size(); ++i)
        root.heaps[i] = m_context.descriptorPool[i]->heap();

    for (size_t i = 0; i < m_ranges.size(); ++i)
    {
        count = 0;
        for (CD3DX12_DESCRIPTOR_RANGE1& r : m_ranges[i])
            count += r.NumDescriptors;
        root.tables[i] = m_context.descriptorPool[i]->allocate(count);
    }
}

void Pipeline::updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, TexturePtr& texture)
{
    auto* image = checked_cast<Texture*>(texture.get());
    auto* native = checked_cast<Sampler*>(sampler.get());

    // Describe and create a SRV for the resource.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = image->desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = image->desc.MipLevels;

    DescriptorSet& descriptorSet = m_descriptors[descriptor];
    // assert(!descriptorSet.tables[0].isNull());
    assert(!descriptorSet.tables[1].isNull());
    // assert(binding < descriptorSet.bindings.size());
    // uint32_t index = descriptorSet.bindings[binding];
    // m_context.device->CreateShaderResourceView(image->handle, &srvDesc,
    // descriptorSet.tables[0].getCpuHandle(binding));
    m_context.device->CreateSampler(&native->desc, descriptorSet.tables[1].getCpuHandle());
}

void Pipeline::updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler,
                             const std::span<TexturePtr>& textures)
{
    auto* native = checked_cast<Sampler*>(sampler.get());

    DescriptorSet& descriptorSet = m_descriptors[descriptor];
    assert(!descriptorSet.tables[0].isNull());
    assert(!descriptorSet.tables[1].isNull());
    m_context.device->CreateSampler(&native->desc, descriptorSet.tables[1].getCpuHandle());

    for (size_t i = 0; i < textures.size(); ++i)
    {
        auto& tex = textures[i];
        auto* image = checked_cast<Texture*>(tex.get());
        // Describe and create a SRV for the resource.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = image->desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = image->desc.MipLevels;

        m_context.device->CreateShaderResourceView(image->handle, &srvDesc,
                                                   descriptorSet.tables[0].getCpuHandle(binding + i));
    }
}

void Pipeline::updateStorage(uint32_t descriptor, uint32_t binding, BufferPtr& buffer, uint64_t byteSize)
{
    auto* native = checked_cast<Buffer*>(buffer.get());
    DescriptorSet& descriptorSet = m_descriptors[descriptor];

    auto range = bindingMap.find(binding);
    ShaderBindDesc& b = range->second;

    if (native->isCBV)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = native->handle->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = native->sizeInBytes();

        m_context.device->CreateConstantBufferView(&cbvDesc, descriptorSet.tables[0].getCpuHandle(binding));
    }
    else
    {
        // uint32_t stride = structureByteStride[binding];
        uint32_t stride = b.stride;
        if (native->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS && byteSize == 256)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            if (stride < UINT32_MAX)
            {
                uavDesc.Buffer.StructureByteStride = stride;
                uavDesc.Buffer.NumElements = native->sizeInBytes() / stride;
            }
            else
            {
                uavDesc.Format = DXGI_FORMAT_R32_UINT;
                uavDesc.Buffer.NumElements = native->sizeInBytes() / sizeof(uint32_t);
            }

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = native->clearCpuHandle.getCpuHandle();
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleBis = native->clearGpuHandle.getCpuHandle();
            m_context.device->CreateUnorderedAccessView(native->handle, nullptr, &uavDesc, cpuHandle);
            m_context.device->CopyDescriptorsSimple(1, cpuHandleBis, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            m_context.device->CopyDescriptorsSimple(1, descriptorSet.tables[0].getCpuHandle(binding), cpuHandle,
                                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // m_context.device->CreateUnorderedAccessView(native->handle, nullptr, &uavDesc,
            // descriptorSet.tables[0].getCpuHandle(binding)); m_context.device->CopyDescriptorsSimple(1,
            // native->clearCpuHandle.getCpuHandle(), descriptorSet.tables[0].getCpuHandle(binding),
            // D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); m_context.device->CopyDescriptorsSimple(1,
            // native->clearGpuHandle.getCpuHandle(), descriptorSet.tables[0].getCpuHandle(binding),
            // D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        else
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.StructureByteStride = stride;
            srvDesc.Buffer.NumElements = native->sizeInBytes() / stride;

            m_context.device->CreateShaderResourceView(native->handle, &srvDesc,
                                                       descriptorSet.tables[0].getCpuHandle(binding));
        }
    }
}
} // namespace ler::rhi::d3d12