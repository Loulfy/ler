//
// Created by loulfy on 03/12/2023.
//

#include "rhi/ref.hpp"
#include "rhi/rhi.hpp"

#ifdef _WIN32
#include <d3d12shader.h>
#endif
#include <dxcapi.h>
#include <fstream>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/SPIRV/doc.h>

namespace ler::rhi
{
//using Microsoft::WRL::ComPtr;

struct KindMapping
{
    fs::path ext;
    ShaderType type;
};

static const std::array<KindMapping, 4> c_KindMap = { {
    { ".vert", ShaderType::Vertex },
    { ".frag", ShaderType::Pixel },
    { ".geom", ShaderType::Geometry },
    { ".comp", ShaderType::Compute },
} };

std::optional<KindMapping> convertShaderStageToExtension(const ShaderType& stage)
{
    for (auto& map : c_KindMap)
        if (map.type == stage)
            return map;
    return {};
}

void from_json(const json& j, ShaderModule& s)
{
    if (j.contains("path"))
        s.path = fs::path(j["path"].get<std::string>());
    if (j.contains("name"))
        s.name = j["name"];
    if (j.contains("entryPoint"))
        s.entryPoint = j["entryPoint"];
    if (j.contains("stage"))
    {
        std::string stage = j["stage"];
        if (stage == "Vertex")
            s.stage = ShaderType::Vertex;
        else if (stage == "Pixel")
            s.stage = ShaderType::Pixel;
        else if (stage == "Compute")
            s.stage = ShaderType::Compute;
    }
    if (j.contains("backend"))
    {
        std::string backend = j["backend"];
        if (backend == "vulkan")
            s.backend = GraphicsAPI::VULKAN;
        else if (backend == "d3d12")
            s.backend = GraphicsAPI::D3D12;
    }
}

static EShLanguage convertShaderStageGlsl(ShaderType type)
{
    switch (type)
    {
    case ShaderType::Compute:
        return EShLanguage::EShLangCompute;
    case ShaderType::Vertex:
        return EShLanguage::EShLangVertex;
    case ShaderType::Hull:
        return EShLanguage::EShLangTessControl;
    case ShaderType::Domain:
        return EShLanguage::EShLangTessEvaluation;
    case ShaderType::Geometry:
        return EShLanguage::EShLangGeometry;
    case ShaderType::Pixel:
        return EShLanguage::EShLangFragment;
    case ShaderType::Mesh:
        return EShLanguage::EShLangMesh;
    case ShaderType::RayGeneration:
        return EShLanguage::EShLangRayGen;
    case ShaderType::AnyHit:
        return EShLanguage::EShLangAnyHit;
    case ShaderType::ClosestHit:
        return EShLanguage::EShLangClosestHit;
    case ShaderType::Miss:
        return EShLanguage::EShLangMiss;
    case ShaderType::Intersection:
        return EShLanguage::EShLangIntersect;
    case ShaderType::Callable:
        return EShLanguage::EShLangCallable;

    default:
    case ShaderType::None:
    case ShaderType::All:
    case ShaderType::AllGraphics:
    case ShaderType::Amplification:
    case ShaderType::AllRayTracing:
        return EShLanguage::EShLangCount;
    }
}

static std::wstring convertShaderStageHlsl(ShaderType type)
{
    switch (type)
    {
    case ShaderType::Compute:
        return L"cs_6_6";
    case ShaderType::Vertex:
        return L"vs_6_6";
    case ShaderType::Hull:
        return L"hs_6_5";
    case ShaderType::Domain:
        return L"ds_6_5";
    case ShaderType::Geometry:
        return L"gs_6_5";
    case ShaderType::Pixel:
        return L"ps_6_6";
    case ShaderType::Mesh:
        return L"ms_6_5";
    case ShaderType::RayGeneration:
    case ShaderType::AnyHit:
    case ShaderType::ClosestHit:
    case ShaderType::Miss:
    case ShaderType::Intersection:
    case ShaderType::Callable:
    case ShaderType::AllRayTracing:
        return L"as_6_5";
    default:
    case ShaderType::None:
    case ShaderType::All:
    case ShaderType::AllGraphics:
    case ShaderType::Amplification:
        return L"lib_6_5";
    }
}

ShaderModule convertShaderExtension(const fs::path& path)
{
    ShaderModule shader;
    shader.path = path;
    shader.entryPoint = "PSMain";
    shader.stage = ShaderType::Pixel;
    return shader;
}

static void compileShaderHlsl(const ShaderModule& shaderModule, const fs::path& output)
{
    std::wstring targetProfile = convertShaderStageHlsl(shaderModule.stage);
    std::wstring entryPoint = sys::toUtf16(shaderModule.entryPoint);

    RefPtr<IDxcCompiler> compiler;
    RefPtr<IDxcUtils> utils;
    RefPtr<IDxcIncludeHandler> includeHandler;
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    utils->CreateDefaultIncludeHandler(&includeHandler);

    std::vector<LPCWSTR> compilationArguments{
        // DXC_ARG_PACK_MATRIX_ROW_MAJOR,
        DXC_ARG_WARNINGS_ARE_ERRORS,
        DXC_ARG_ALL_RESOURCES_BOUND,
        L"-Qembed_debug",
        L"-fspv-extension=SPV_KHR_ray_tracing",
        L"-fspv-extension=SPV_KHR_multiview",
        L"-fspv-extension=SPV_KHR_shader_draw_parameters",
        L"-fspv-extension=SPV_EXT_descriptor_indexing",
        L"-fspv-extension=SPV_KHR_ray_query"
        //L"-fvk-bind-resource-heap"
    };

    compilationArguments.push_back(DXC_ARG_DEBUG);
    // compilationArguments.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);

    uint32_t defineCount = 0;
    DxcDefine spirv(L"VK", L"1");
    if (output.extension() == ".spv")
    {
        compilationArguments.push_back(L"-spirv");
        if (shaderModule.stage == ShaderType::Vertex)
            compilationArguments.push_back(L"-fvk-invert-y");
        defineCount = 1;
    }

    const fs::path path = sys::ASSETS_DIR / shaderModule.path;

    // Load the shader source file to a blob.
    RefPtr<IDxcBlobEncoding> sourceBlob;
    std::wstring source = sys::toUtf16(path.string());
    HRESULT hr = utils->LoadFile(source.c_str(), nullptr, &sourceBlob);
    if (FAILED(hr))
        log::error("Failed to open shader with path : {}", shaderModule.path.string());

    // Compile the shader.
    RefPtr<IDxcOperationResult> result;
    hr = compiler->Compile(
        sourceBlob.Get(), source.c_str(), entryPoint.c_str(), targetProfile.c_str(), compilationArguments.data(),
        static_cast<uint32_t>(compilationArguments.size()), &spirv, defineCount, includeHandler.Get(), &result);
    if (FAILED(hr))
    {
        log::error("Failed to compile shader with path : {}", shaderModule.path.string());
    }

    // Get compilation errors (if any).
    RefPtr<IDxcBlobEncoding> errors;
    result->GetErrorBuffer(&errors);
    if (errors && errors->GetBufferSize() > 0)
    {
        LPVOID errorMessage = errors->GetBufferPointer();
        log::error(std::string(static_cast<LPCSTR>(errorMessage)));
        return;
    }

    RefPtr<IDxcBlob> bytecode;
    result->GetResult(&bytecode);

    std::ofstream file(output, std::ios::out | std::ios::binary);
    file.write(static_cast<char*>(bytecode->GetBufferPointer()), int32_t(bytecode->GetBufferSize()));
    file.close();
}

static bool compile(glslang::TShader* shader, const std::string& code, EShMessages controls, const std::string& shaderName, const std::string& entryPointName)
{
    const char* shaderStrings = code.c_str();
    const int shaderLengths = static_cast<int>(code.size());
    const char* shaderNames = shaderName.c_str();

    if (controls & EShMsgDebugInfo)
    {
        shaderNames = shaderName.data();
        shader->setStringsWithLengthsAndNames(&shaderStrings, &shaderLengths, &shaderNames, 1);
    }
    else
    {
        shader->setStringsWithLengths(&shaderStrings, &shaderLengths, 1);
    }

    if (!entryPointName.empty())
        shader->setEntryPoint(entryPointName.c_str());
    return shader->parse(GetDefaultResources(), 110, false, controls);
}

static void compileShaderGlsl(const ShaderModule& shaderModule, const fs::path& output)
{
    EShLanguage stage = convertShaderStageGlsl(shaderModule.stage);

    const fs::path path = sys::ASSETS_DIR / shaderModule.path;
    std::vector<char> blob = sys::readBlobFile(path);
    std::string code(blob.begin(), blob.end());

    bool success = true;
    glslang::TShader shader(stage);
    shader.setInvertY(false);
    EShMessages controls = EShMsgCascadingErrors;
    controls = static_cast<EShMessages>(controls | EShMsgDebugInfo);
    controls = static_cast<EShMessages>(controls | EShMsgSpvRules);
    controls = static_cast<EShMessages>(controls | EShMsgKeepUncalled);
    controls = static_cast<EShMessages>(controls | EShMsgVulkanRules | EShMsgSpvRules);
    shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetLanguageVersion::EShTargetSpv_1_6);
    success &= compile(&shader, code, controls, shaderModule.name, shaderModule.entryPoint);

    if(!success)
    {
        log::error(shader.getInfoLog());
        return;
    }

    // Link all of them.
    glslang::TProgram program;
    program.addShader(&shader);
    success &= program.link(controls);

    if(!success)
    {
        log::error(program.getInfoLog());
        return;
    }

    glslang::SpvOptions options;
    spv::SpvBuildLogger logger;
    std::vector<uint32_t> spv;
    //options.disableOptimizer = false;
    //options.optimizeSize = true;
    options.stripDebugInfo = false;
    options.emitNonSemanticShaderDebugInfo = true;
    options.emitNonSemanticShaderDebugSource = true;
    glslang::GlslangToSpv(*program.getIntermediate(shader.getStage()), spv, &logger, &options);
    if(!logger.getAllMessages().empty())
        log::error(logger.getAllMessages());

    std::ofstream file(output, std::ios::out | std::ios::binary);
    auto size = static_cast<std::streamsize>(spv.size() * sizeof(uint32_t));
    file.write(reinterpret_cast<char*>(spv.data()), size);
    file.close();
}

void IDevice::compileShader(const ShaderModule& shaderModule, const fs::path& output)
{
    if(shaderModule.path.extension() == ".hlsl")
        compileShaderHlsl(shaderModule, output);
    if(shaderModule.path.extension() == ".glsl")
        compileShaderGlsl(shaderModule, output);
}

void IDevice::shaderAutoCompile()
{
    glslang::InitializeProcess();
    fs::create_directory(sys::CACHED_DIR);

    std::ifstream bundle(sys::ASSETS_DIR / "shaders.json");
    std::stringstream buffer;
    buffer << bundle.rdbuf();
    json j = json::parse(buffer.str());
    std::vector<ShaderModule> shaderModules = j;

    for (auto const& shaderModule : shaderModules)
    {
        const fs::path& entry = shaderModule.path;
        const auto& res = convertShaderStageToExtension(shaderModule.stage);
        const std::string filename = shaderModule.name + res.value().ext.string();
        fs::path f = sys::CACHED_DIR / filename;
        switch (shaderModule.backend)
        {

        case GraphicsAPI::D3D12:
            f.concat(".cso");
            break;
        case GraphicsAPI::VULKAN:
            f.concat(".spv");
            break;
        }

        if(fs::exists(f) && fs::last_write_time(f) > fs::last_write_time(sys::ASSETS_DIR / entry))
            continue;

        log::warn("Compile shader: {} to {}", entry.string(), f.string());
        compileShader(shaderModule, f);
    }
    glslang::FinalizeProcess();
}
} // namespace ler::rhi