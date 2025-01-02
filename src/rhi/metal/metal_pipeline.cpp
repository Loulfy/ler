//
// Created by Loulfy on 30/12/2024.
//

#include "rhi/metal.hpp"

#define IR_RUNTIME_METALCPP       // enable metal-cpp compatibility mode
#define IR_PRIVATE_IMPLEMENTATION // define only once in an implementation file

#include <metal_irconverter/metal_irconverter.h>
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
    IRCompilerSetMinimumDeploymentTarget(pCompiler, IROperatingSystem_macOS, "15.0.0");
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

static std::string_view resourceTypeToString(IRResourceType resType)
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
    case IRResourceTypeInvalid:
        return "Invalid";
    }
}

PipelinePtr Device::createGraphicsPipeline(const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc)
{
    return nullptr;
}

PipelinePtr Device::createComputePipeline(const ShaderModule& shaderModule)
{
    IRVersionedRootSignatureDescriptor rootSigDesc;
    memset(&rootSigDesc, 0x0, sizeof(IRVersionedRootSignatureDescriptor));
    rootSigDesc.version = IRRootSignatureVersion_1_1;
    // rootSigDesc.desc_1_1.NumParameters = sizeof(params) / sizeof(IRRootParameter1);
    // rootSigDesc.desc_1_1.pParameters = params;
    // rootSigDesc.desc_1_1.pStaticSamplers = samps;
    // rootSigDesc.desc_1_1.NumStaticSamplers = sizeof(samps) / sizeof(IRStaticSamplerDescriptor);

    IRError* pRootSigError = nullptr;
    IRRootSignature* pRootSig = IRRootSignatureCreateFromDescriptor(&rootSigDesc, &pRootSigError);
    assert(pRootSig);

    MTLib lib = newLibraryWithReflectionFromDXIL(shaderModule.path, IRShaderStageCompute, shaderModule.entryPoint,
                                                 nullptr, m_device);

    // Determine whether draw params are needed:
    IRVersionedVSInfo vsinfo;
    if (IRShaderReflectionCopyVertexInfo(lib.reflection, IRReflectionVersion_1_0, &vsinfo))
    {
        if (vsinfo.info_1_0.needs_draw_params)
        {
            // PSO needs a draw params buffer bound to the vertex stage
        }
    }
    IRShaderReflectionReleaseVertexInfo(&vsinfo);

    std::vector<IRResourceLocation> resourceLocations(IRShaderReflectionGetResourceCount(lib.reflection));
    IRShaderReflectionGetResourceLocations(lib.reflection, resourceLocations.data());

    log::debug("======================================================");
    log::debug("Reflect Shader: {}, Stage: {}", shaderModule.path.stem().string(), to_string(shaderModule.stage));

    for (const IRResourceLocation& res : resourceLocations)
    {
        log::debug("space = {}, slot = {}, sizeBytes = {:02}, type = {}", res.space, res.slot, res.sizeBytes, resourceTypeToString(res.resourceType));
        switch (res.resourceType)
        {
        case IRResourceTypeConstant:
        case IRResourceTypeCBV:
            break;
        default:
            log::error("Resource type not supported: {}", resourceTypeToString(res.resourceType));
        }
    }

    log::info(IRShaderReflectionCopyJSONString(lib.reflection));

    NS::Error* mtlError = nil;
    MTL::Function* func = lib.library->newFunction(NS::String::string(shaderModule.entryPoint.c_str(), NS::StringEncoding::UTF8StringEncoding));
    
    MTL::ComputePipelineState* pso =  m_device->newComputePipelineState(func, &mtlError);
    int t = kIRDescriptorHeapBindPoint;

    return nullptr;
}
} // namespace ler::rhi::metal