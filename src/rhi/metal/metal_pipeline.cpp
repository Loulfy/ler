//
// Created by Loulfy on 30/12/2024.
//

#include "rhi/metal.hpp"
#include "rhi/ref.hpp"

#include <metal_irconverter/metal_irconverter.h>
#define IR_RUNTIME_METALCPP
#include <metal_irconverter_runtime/metal_irconverter_runtime.h>

namespace ler::rhi::metal
{
static IRObject* newDXILObject(const std::string& dxilPath)
{
    fs::path path = dxilPath;
    path.concat(".cso");

    std::vector<char> bytecode = sys::readBlobFile(path);

    const auto* dxilBytecode = reinterpret_cast<uint8_t*>(bytecode.data());
    IRObject* pDXIL = IRObjectCreateFromDXIL(dxilBytecode, bytecode.size(), IRBytecodeOwnershipCopy);
    return pDXIL;
}

struct MTLib
{
    NS::SharedPtr<MTL::Library> library;
    IRShaderReflection* reflection = nullptr;

    ~MTLib()
    {
        if (reflection)
            IRShaderReflectionDestroy(reflection);
    }
};

/*static void reflectFromDXIL(const std::string& dxilPath)
{
    RefPtr<IDxcUtils> utils;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));

    RefPtr<IDxcBlobEncoding> sourceBlob;
    utils->LoadFile(dxilPath.c_str(), nullptr, &sourceBlob);
    //shader->bytecode = sourceBlob;

    const DxcBuffer reflectionBuffer(sourceBlob->GetBufferPointer(), sourceBlob->GetBufferSize(), 0u);

    RefPtr<ID3D12ShaderReflection> shaderReflection;
    utils->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&shaderReflection));
    D3D12_SHADER_DESC shaderDesc{};
    shaderReflection->GetDesc(&shaderDesc);
}*/

static MTLib newLibraryWithReflectionFromDXIL(const std::string& dxilPath, IRShaderStage shaderStage,
                                              const std::string& entryPointName, const IRRootSignature* pRootSig,
                                              MTL::Device* pDevice)
{
    MTLib outMetalLib;

    // Load the DXIL file to memory.
    IRObject* pDXIL = newDXILObject(dxilPath);

    // Create the IRConverter compiler to compile DXIL to Metal IR.
    IRError* pCompError = nil;
    IRCompiler* pCompiler = IRCompilerCreate();

    // Configure the IRConverter compiler to set the minimum deployment target and
    // enable geometry stage emulation if the caller requests it.
    // IRCompilerSetMinimumDeploymentTarget(pCompiler, IROperatingSystem_macOS, "15.0.0");
    IRCompilerSetGlobalRootSignature(pCompiler, pRootSig);
    IRCompilerSetEntryPointName(pCompiler, entryPointName.c_str());

    // Compile DXIL to a Metal IR object.
    IRObject* pAIR = IRCompilerAllocCompileAndLink(pCompiler, nullptr, pDXIL, &pCompError);

    // Check for compilation errors.
    if (!pAIR)
    {
        log::error("Error compiling shader to AIR: {}", static_cast<const char*>(IRErrorGetPayload(pCompError)));

        // Free resources.
        IRErrorDestroy(pCompError);
        IRCompilerDestroy(pCompiler);
        IRObjectDestroy(pDXIL);

        // Return a null tuple upon encountering an error.
        return outMetalLib;
    }

    {
        // Obtain the metal lib from the Metal IR.
        IRMetalLibBinary* pMetallibBin = IRMetalLibBinaryCreate();
        if (IRObjectGetMetalLibBinary(pAIR, shaderStage, pMetallibBin))
        {
            size_t metallibSize = IRMetalLibGetBytecodeSize(pMetallibBin);
            auto d = new uint8_t[metallibSize];
            IRMetalLibGetBytecode(pMetallibBin, d);
            std::ofstream f("test.metallib");
            f.write(reinterpret_cast<char*>(d), metallibSize);
            f.close();
            dispatch_data_t metallibData = IRMetalLibGetBytecodeData(pMetallibBin);
            NS::Error* pMtlError = nil;
            outMetalLib.library = NS::TransferPtr(pDevice->newLibrary(metallibData, &pMtlError));

            if (!outMetalLib.library)
                log::error(pMtlError->localizedDescription()->utf8String());

            IRMetalLibBinaryDestroy(pMetallibBin);
        }
    }

    {
        outMetalLib.reflection = IRShaderReflectionCreate();
        IRObjectGetReflection(pAIR, shaderStage, outMetalLib.reflection);
    }

    // Free resources.
    IRObjectDestroy(pAIR);
    IRCompilerDestroy(pCompiler);
    IRObjectDestroy(pDXIL);

    // Return metal lib and reflection.
    return outMetalLib;
}

static IRShaderStage getShaderStage(ShaderType type)
{
    switch (type)
    {
    default:
        return IRShaderStageInvalid;
    case ShaderType::Compute:
        return IRShaderStageCompute;
    case ShaderType::Vertex:
        return IRShaderStageVertex;
    case ShaderType::Hull:
        return IRShaderStageHull;
    case ShaderType::Domain:
        return IRShaderStageDomain;
    case ShaderType::Geometry:
        return IRShaderStageGeometry;
    case ShaderType::Pixel:
        return IRShaderStageFragment;
    case ShaderType::Amplification:
        return IRShaderStageAmplification;
    case ShaderType::Mesh:
        return IRShaderStageMesh;
    case ShaderType::RayGeneration:
        return IRShaderStageRayGeneration;
    case ShaderType::AnyHit:
        return IRShaderStageAnyHit;
    case ShaderType::ClosestHit:
        return IRShaderStageClosestHit;
    case ShaderType::Miss:
        return IRShaderStageMiss;
    case ShaderType::Intersection:
        return IRShaderStageIntersection;
    case ShaderType::Callable:
        return IRShaderStageCallable;
    }
}

static MTL::TriangleFillMode getFillMode(RasterFillMode rasterFillMode)
{
    switch (rasterFillMode)
    {
    default:
    case RasterFillMode::Solid:
        return MTL::TriangleFillModeFill;
    case RasterFillMode::Wireframe:
        return MTL::TriangleFillModeLines;
    }
}

static constexpr std::string_view resourceTypeToString(IRResourceType resType)
{
    switch (resType)
    {
    case IRResourceTypeTable:
        return "Table";
    case IRResourceTypeConstant:
        return "Constant";
    case IRResourceTypeCBV:
        return "CBV";
    case IRResourceTypeSRV:
        return "SRV";
    case IRResourceTypeUAV:
        return "UAV";
    case IRResourceTypeSampler:
        return "Sampler";
    default:
    case IRResourceTypeInvalid:
        return "Invalid";
    }
}

struct VtxInfo
{
    uint32_t index = 0u;
    uint32_t columnCount = 0u;
    std::string elementType;
    std::string name;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(VtxInfo, columnCount, elementType, name, index)
};

struct VtxFormatMapping
{
    MTL::VertexFormat format = MTL::VertexFormatFloat;
    std::string name = "Float";
    uint32_t sizeInBytes = 4u;
    uint32_t columnCount = 1u;
};

static const std::array<VtxFormatMapping, 16> c_FormatMap = { {
    { MTL::VertexFormatFloat, "Float", 4, 1 },
    { MTL::VertexFormatFloat2, "Float", 8, 2 },
    { MTL::VertexFormatFloat3, "Float", 12, 3 },
    { MTL::VertexFormatFloat4, "Float", 16, 4 },
    { MTL::VertexFormatUInt, "UInt", 4, 1 },
    { MTL::VertexFormatUInt2, "UInt", 8, 2 },
    { MTL::VertexFormatUInt3, "UInt", 12, 3 },
    { MTL::VertexFormatUInt4, "UInt", 16, 4 },
    { MTL::VertexFormatInt, "Int", 4, 1 },
    { MTL::VertexFormatInt2, "Int", 8, 2 },
    { MTL::VertexFormatInt3, "Int", 12, 3 },
    { MTL::VertexFormatInt4, "Int", 16, 4 },
    { MTL::VertexFormatUShort, "UShort", 2, 1 },
    { MTL::VertexFormatUShort2, "UShort", 4, 2 },
    { MTL::VertexFormatUShort3, "UShort", 6, 3 },
    { MTL::VertexFormatUShort4, "UShort", 8, 4 },
} };

static constexpr VtxFormatMapping getVtxFormat(const VtxInfo& info)
{
    for (const VtxFormatMapping& format : c_FormatMap)
    {
        if (format.name == info.elementType && format.columnCount == info.columnCount)
            return format;
    }
    return {};
}

void printJsonReflection(const json& j)
{
    for (const auto& topLevel : j["TopLevelArgumentBuffer"])
        log::debug(topLevel.dump());
}

PipelinePtr Device::createGraphicsPipeline(const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc)
{
    auto pipeline = std::make_shared<BasePipeline>(m_context);

    MTL::RenderPipelineDescriptor* pDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    for (NS::UInteger i = 0; i < desc.colorAttach.size(); ++i)
    {
        pDesc->colorAttachments()->object(i)->setPixelFormat(convertFormat(desc.colorAttach[i]));
        pDesc->colorAttachments()->object(i)->setBlendingEnabled(true);
        pDesc->colorAttachments()->object(i)->setWriteMask(MTL::ColorWriteMaskAll);
    }
    if (desc.writeDepth)
    {
        MTL::DepthStencilDescriptor* pDsDesc = MTL::DepthStencilDescriptor::alloc()->init();
        pDsDesc->setDepthCompareFunction(MTL::CompareFunction::CompareFunctionLessEqual);
        pDsDesc->setDepthWriteEnabled(true);
        pipeline->depthStencilState = NS::TransferPtr(m_context.device->newDepthStencilState(pDsDesc));
        pDesc->setDepthAttachmentPixelFormat(convertFormat(desc.depthAttach));
    }
    pDesc->setInputPrimitiveTopology(MTL::PrimitiveTopologyClassTriangle);

    for (const ShaderModule& shader : shaderModules)
    {
        MTLib lib = newLibraryWithReflectionFromDXIL(shader.path, getShaderStage(shader.stage), shader.entryPoint,
                                                     m_context.rootSignature, m_device);

        log::debug("======================================================");
        log::debug("Reflect Shader: {}, Stage: {}", shader.path.stem().string(), to_string(shader.stage));

        std::vector<VtxInfo> vertexInfos;
        if (lib.reflection && shader.stage == ShaderType::Vertex)
        {
            const std::string_view info = IRShaderReflectionCopyJSONString(lib.reflection);
            log::debug(info);
            json j = json::parse(info);
            printJsonReflection(j);
            vertexInfos = j["vertex_inputs"];
            IRShaderReflectionReleaseString(info.data());
        }

        MTL::Function* func = lib.library->newFunction(
            NS::String::string(shader.entryPoint.c_str(), NS::StringEncoding::UTF8StringEncoding));

        if (shader.stage == ShaderType::Vertex)
        {
            pDesc->setVertexFunction(func);
            const MTL::VertexDescriptor* vtxDesc = MTL::VertexDescriptor::alloc()->init();
            for (const VtxInfo& vtxInfo : vertexInfos)
            {
                const VtxFormatMapping& vtxFormat = getVtxFormat(vtxInfo);
                MTL::VertexAttributeDescriptor* attributeDesc =
                    vtxDesc->attributes()->object(kIRStageInAttributeStartIndex + vtxInfo.index);
                attributeDesc->setFormat(vtxFormat.format);
                attributeDesc->setBufferIndex(kIRVertexBufferBindPoint + vtxInfo.index);
                attributeDesc->setOffset(0);

                MTL::VertexBufferLayoutDescriptor* layoutDesc =
                    vtxDesc->layouts()->object(kIRVertexBufferBindPoint + vtxInfo.index);
                layoutDesc->setStepFunction(MTL::VertexStepFunctionPerVertex);
                layoutDesc->setStride(vtxFormat.sizeInBytes);
                layoutDesc->setStepRate(1);
            }
            pDesc->setVertexDescriptor(vtxDesc);
        }
        else if (shader.stage == ShaderType::Pixel)
            pDesc->setFragmentFunction(func);

        func->release();
    }

    NS::Error* error = nil;
    pDesc->setSupportIndirectCommandBuffers(desc.indirectDraw);
    const MTL::AutoreleasedRenderPipelineReflection* reflection = nullptr;
    pipeline->renderPipelineState = NS::TransferPtr(m_device->newRenderPipelineState(pDesc, MTL::PipelineOptionBufferTypeInfo, reflection, &error));
    pipeline->fillMode = getFillMode(desc.fillMode);
    pDesc->release();
    if (!pipeline->renderPipelineState)
    {
        log::error(error->localizedDescription()->utf8String());
        assert(false);
    }

    return pipeline;
}

PipelinePtr Device::createComputePipeline(const ShaderModule& shaderModule)
{
    MTLib lib = newLibraryWithReflectionFromDXIL(shaderModule.path, IRShaderStageCompute, shaderModule.entryPoint,
                                                 m_context.rootSignature, m_device);

    log::debug("======================================================");
    log::debug("Reflect Shader: {}, Stage: {}", shaderModule.path.stem().string(), to_string(shaderModule.stage));

    const std::string_view info = IRShaderReflectionCopyJSONString(lib.reflection);
    log::debug(info);
    IRShaderReflectionReleaseString(info.data());

    NS::Error* mtlError = nil;
    MTL::Function* func = lib.library->newFunction(
        NS::String::string(shaderModule.entryPoint.c_str(), NS::StringEncoding::UTF8StringEncoding));

    auto pipeline = std::make_shared<BasePipeline>(m_context);
    pipeline->computePipelineState = NS::TransferPtr(m_device->newComputePipelineState(func, &mtlError));
    pipeline->m_pipelineType = BasePipeline::PipelineTypeCompute;

    IRVersionedCSInfo csinfo;
    IRShaderReflectionCopyComputeInfo(lib.reflection, IRReflectionVersion_1_0, &csinfo);
    pipeline->threadGroupSize.width = csinfo.info_1_0.tg_size[0];
    pipeline->threadGroupSize.height = csinfo.info_1_0.tg_size[1];
    pipeline->threadGroupSize.depth = csinfo.info_1_0.tg_size[2];
    IRShaderReflectionReleaseComputeInfo(&csinfo);

    return pipeline;
}

GraphicsPipeline::GraphicsPipeline(const MetalContext& context) : BasePipeline(context)
{
}

ComputePipeline::ComputePipeline(const MetalContext& context) : BasePipeline(context)
{
}


} // namespace ler::rhi::metal