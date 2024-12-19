//
// Created by loulfy on 04/10/2024.
//

#include "rhi/d3d12.hpp"

namespace ler::rhi::d3d12
{
PipelinePtr Device::loadPipeline(const std::string& name, const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc)
{
    return m_library->loadPipeline(name, shaderModules, desc);
}

MappedFile::~MappedFile()
{
    close();
}

void MappedFile::open(const std::wstring& path)
{
    m_file = CreateFile2(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, OPEN_EXISTING, nullptr);
    if (m_file == INVALID_HANDLE_VALUE)
        return;

    LARGE_INTEGER lpFileSize;
    BOOL flag = GetFileSizeEx(m_file, &lpFileSize);
    assert(flag == 1);

    assert(lpFileSize.HighPart == 0);
    m_mapping = CreateFileMappingA(m_file, nullptr, PAGE_READWRITE, 0, lpFileSize.LowPart, nullptr);

    m_mappedData = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, lpFileSize.LowPart);
    m_mappedSize = lpFileSize.LowPart;
}

void MappedFile::close()
{
    if (m_mappedData)
    {
        BOOL flag = UnmapViewOfFile(m_mappedData);
        assert(flag == 1);
        m_mappedData = nullptr;

        flag = CloseHandle(m_mapping); // Close the file mapping object.
        assert(flag == 1);

        flag = CloseHandle(m_file); // Close the file itself.
        assert(flag == 1);
    }
}

void MappedFile::reset(const std::wstring& path)
{
    close();
    open(path);
}

void* MappedFile::getData() const
{
    return m_mappedData;
}

uint64_t MappedFile::getSize() const
{
    return m_mappedSize;
}

PSOLibrary::~PSOLibrary()
{
    destroy();
}

PSOLibrary::PSOLibrary(Device* device) : m_device(device)
{
    const D3D12Context& context = device->getContext();

    DXGI_ADAPTER_DESC desc = {};
    HRESULT hr = context.adapter->GetDesc(&desc);
    std::string filename;
    if (SUCCEEDED(hr))
        filename = fmt::format("{:04x}{:04x}{:08x}{:04x}", desc.VendorId, desc.DeviceId, desc.SubSysId, desc.Revision);

    fs::path path = sys::CACHED_DIR / filename;
    path.replace_extension(kLibExt);
    m_pathLib = sys::toUtf16(path.string());

    std::ifstream bundle(sys::ASSETS_DIR / "pipelines.json");
    std::stringstream buffer;
    buffer << bundle.rdbuf();
    json j = json::parse(buffer.str());
    std::vector<PsoCache> pipelines = j;

    std::ranges::transform(
        std::views::as_rvalue(pipelines), std::inserter(m_pipelines, m_pipelines.end()),
        [](PsoCache&& p) -> std::pair<std::string, PsoCache> { return std::make_pair(p.name, std::move(p)); });

    // Create the Pipeline Library.
    ComPtr<ID3D12Device1> device1;
    if (SUCCEEDED(context.device->QueryInterface(IID_PPV_ARGS(&device1))))
    {
        // Init the memory mapped file.
        m_mappedFile.reset(m_pathLib);

        // Create a Pipeline Library from the serialized blob.
        // Note: The provided Library Blob must remain valid for the lifetime of the object returned - for efficiency,
        // the data is not copied.
        hr = device1->CreatePipelineLibrary(m_mappedFile.getData(), m_mappedFile.getSize(),
                                            IID_PPV_ARGS(&m_pipelineLibrary));
        switch (hr)
        {
        case E_INVALIDARG:
        case D3D12_ERROR_ADAPTER_NOT_FOUND:
        case D3D12_ERROR_DRIVER_VERSION_MISMATCH:
            hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&m_pipelineLibrary));
            assert(SUCCEEDED(hr));
            break;

        case NOERROR:
            break;
        case DXGI_ERROR_UNSUPPORTED: // The driver doesn't support Pipeline libraries.
        default:
            log::exit(getErrorMsg(hr));
        }

        for (PsoCache& p : std::views::values(m_pipelines))
            build(p);
    }
}

void PSOLibrary::destroy()
{
    if (m_pipelineLibrary)
    {
        std::vector<char> blob(m_pipelineLibrary->GetSerializedSize());
        m_pipelineLibrary->Serialize(blob.data(), blob.size());
        m_mappedFile.close();
        std::ofstream file(m_pathLib, std::ios::binary | std::ios::trunc);
        file.write(blob.data(), std::streamsize(blob.size()));
        file.close();

        std::ofstream out("cached/pipelines.json", std::ios::trunc);
        json j = std::views::values(m_pipelines) | std::ranges::to<std::vector>();
        out << j.dump(2);
        out.close();
    }
}


void PSOLibrary::build(PsoCache& psoCache)
{
    std::vector<ShaderPtr> shaders;
    for (const ShaderModule& shaderModule : psoCache.modules)
        shaders.emplace_back(m_device->createShader(shaderModule));

    auto pipeline = std::make_shared<Pipeline>(m_device->getContext());

    bool graphics = true;
    for (const ShaderPtr& shader : shaders)
    {
        pipeline->merge(0, shader->rangesCbvSrvUav);
        pipeline->merge(1, shader->rangesSampler);
        pipeline->bindingMap.merge(shader->bindingMap);
        if (shader->descriptorHeapIndexing)
            pipeline->markDescriptorHeapIndexing();
        if (shader->stage == ShaderType::Compute)
            graphics = false;
    }

    pipeline->initRootSignature(psoCache.desc.indirectDraw);
    pipeline->topology = Device::convertTopology(psoCache.desc.topology);

    std::wstring pipelineName = sys::toUtf16(psoCache.name);
    const D3D12Context& context = m_device->getContext();

    HRESULT hr;
    if (graphics)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        Device::fillGraphicsPsoDesc(psoCache.desc, shaders, psoDesc);
        psoDesc.pRootSignature = pipeline->rootSignature.Get();

        hr = m_pipelineLibrary->LoadGraphicsPipeline(pipelineName.c_str(), &psoDesc,
                                                     IID_PPV_ARGS(&pipeline->pipelineState));
        if (hr == E_INVALIDARG)
        {
            context.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));
            m_pipelineLibrary->StorePipeline(pipelineName.c_str(), pipeline->pipelineState.Get());
        }
    }
    else
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        Device::fillComputePsoDesc(shaders, psoDesc);
        psoDesc.pRootSignature = pipeline->rootSignature.Get();

        hr = m_pipelineLibrary->LoadComputePipeline(pipelineName.c_str(), &psoDesc,
                                                    IID_PPV_ARGS(&pipeline->pipelineState));
        if (hr == E_INVALIDARG)
        {
            context.device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pipelineState));
            m_pipelineLibrary->StorePipeline(pipelineName.c_str(), pipeline->pipelineState.Get());
        }
    }

    psoCache.pipeline = std::move(pipeline);
}

PipelinePtr PSOLibrary::loadPipeline(const std::string& name, const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc)
{
    if (!m_pipelines.contains(name))
    {
        PsoCache pso;
        pso.name = name;
        pso.desc = desc;
        pso.modules.assign(shaderModules.begin(), shaderModules.end());
        build(pso);
        m_pipelines.insert({ name, std::move(pso) });
    }

    PsoCache& pso = m_pipelines.at(name);
    return pso.pipeline;
}
} // namespace ler::rhi::d3d12