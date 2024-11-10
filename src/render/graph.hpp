//
// Created by loulfy on 09/03/2024.
//

#pragma once

#include "pass.hpp"

namespace ler::render
{
/*constexpr static char RT_BackBuffer[] = "backBuffer";

enum RenderResType
{
    RR_ReadOnlyBuffer = 0,
    RR_ConstantBuffer,
    RR_SampledTexture,
    RR_StorageBuffer,
    RR_StorageImage,
    RR_RenderTarget,
    RR_DepthWrite,
    RR_Raytracing,
};

NLOHMANN_JSON_SERIALIZE_ENUM(RenderResType, { { RR_ReadOnlyBuffer, "readOnlyBuffer" },
                                              { RR_ConstantBuffer, "constantBuffer" },
                                              { RR_SampledTexture, "sampledTexture" },
                                              { RR_StorageBuffer, "storageBuffer" },
                                              { RR_StorageImage, "storageImage" },
                                              { RR_RenderTarget, "renderTarget" },
                                              { RR_DepthWrite, "depthWrite" },
                                              { RR_Raytracing, "rayTracing" } })

enum RenderPassType
{
    RP_Graphics,
    RP_Compute,
    RP_RayTracing
};

NLOHMANN_JSON_SERIALIZE_ENUM(RenderPassType, {
                                                 { RP_RayTracing, "raytracing" },
                                                 { RP_Graphics, "graphics" },
                                                 { RP_Compute, "compute" },
                                             })

struct RenderDesc
{
    std::string name;
    uint32_t handle = UINT32_MAX;
    RenderResType type = RR_ReadOnlyBuffer;
    uint32_t binding = UINT32_MAX;
    uint32_t bindlessIndex = UINT32_MAX;

    rhi::BufferDesc buffer;
    rhi::TextureDesc texture;
};

void to_json(json& j, const RenderDesc& r);
void from_json(const json& j, RenderDesc& r);

using RenderResource = std::variant<std::monostate, rhi::BufferPtr, rhi::TexturePtr, rhi::BindlessTablePtr>;

struct RenderPassDesc
{
    std::string name;
    RenderPassType pass = RP_Graphics;
    std::vector<RenderDesc> resources;
};

struct RenderGraphInfo
{
    std::string name;
    std::vector<RenderPassDesc> passes;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RenderPassDesc, name, pass, resources);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RenderGraphInfo, name, passes);

class RenderGraph;
class RenderMeshList;
class RenderGraphPass
{
  public:
    virtual ~RenderGraphPass() = default;
    virtual void create(const rhi::DevicePtr& device, RenderGraph& graph, std::span<RenderDesc> resources) = 0;
    virtual void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport) {};
    virtual void render(rhi::CommandPtr& cmd, RenderParams params) = 0;
    [[nodiscard]] virtual rhi::PipelinePtr getPipeline() const = 0;
    [[nodiscard]] virtual std::string getName() const = 0;
    virtual void getResourceDesc(RenderDesc& desc) = 0;
};

struct RenderGraphNode
{
    size_t index = 0;
    std::string name;
    rhi::RenderingInfo rendering;
    RenderPassType type = RP_Graphics;
    std::vector<RenderDesc> bindings;
    std::unique_ptr<RenderGraphPass> pass;
    uint32_t descriptor = 0u;
    std::set<uint32_t> outputs;
    std::vector<RenderGraphNode*> edges;
};

class RenderGraph : public rhi::IRenderPass, public render::IMeshRenderer
{
  public:
    void parse(const fs::path& path);
    void compile(const rhi::DevicePtr& device);
    void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport) override;
    void execute(rhi::CommandPtr& cmd, rhi::TexturePtr& backBuffer, const RenderParams& params);
    void rebind();

    void addResource(const std::string& name, const RenderResource& res);
    bool getResource(uint32_t handle, rhi::BufferPtr& buffer) const;

    template <typename T> void addPass()
    {
        auto pass = std::make_unique<T>();
        for (RenderGraphNode& node : m_nodes)
        {
            if (node.name == pass->getName())
            {
                node.pass = std::move(pass);
                return;
            }
        }
    }

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override;
    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override;
    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const RenderParams& params) override;

  private:
    RenderGraphInfo m_info;
    std::vector<RenderGraphNode> m_nodes;
    std::vector<RenderResource> m_resourceCache;
    std::unordered_map<std::string, uint32_t> m_resourceMap;

    rhi::BindlessTablePtr m_table;
    rhi::SamplerPtr samplerGlobal;

    void setRenderAttachment(const RenderDesc& desc, rhi::Attachment& info);
    static bool guessOutput(const RenderDesc& res);
    static rhi::ResourceState guessState(const RenderDesc& res);
    void applyBarrier(rhi::CommandPtr& cmd, const RenderDesc& desc, rhi::ResourceState state);
    void bindResource(const rhi::PipelinePtr& pipeline, RenderDesc& res);
    void computeEdges(RenderGraphNode& node);
    void topologicalSort();
};*/

/*enum RenderResType
{
    RR_Buffer = 0,
    RR_Texture,
    RR_Raytracing,
};*/

enum RenderResType
{
    RR_ReadOnlyBuffer = 0,
    RR_ConstantBuffer,
    RR_SampledTexture,
    RR_StorageBuffer,
    RR_StorageImage,
    RR_RenderTarget,
    RR_DepthWrite,
    RR_Raytracing,
};

enum RenderPassType
{
    RP_Graphics,
    RP_Compute,
    RP_RayTracing
};

using RenderResource = std::variant<std::monostate, rhi::BufferPtr, rhi::TexturePtr>;

struct RenderNode
{
    std::string name;
    RenderResType type = RR_StorageBuffer;
    uint32_t bindlessIndex = UINT32_MAX;
    RenderResource resource;
    uint32_t linkGroup = UINT32_MAX;
};

struct RenderGraphTable
{
    std::vector<RenderNode> inputs;
    std::vector<RenderNode> outputs;
};

rhi::BufferPtr getBufferOutput(const RenderGraphTable& res, uint32_t id);

class RenderGraphPass
{
  public:
    virtual ~RenderGraphPass() = default;
    virtual void create(const rhi::DevicePtr& device) = 0;
    virtual void resize(const rhi::DevicePtr& device, const rhi::Extent& viewport) {};
    virtual void render(rhi::CommandPtr& cmd, const RenderParams& params, RenderGraphTable& res) = 0;
    [[nodiscard]] virtual rhi::PipelinePtr getPipeline() const = 0;
    [[nodiscard]] virtual std::string getName() const = 0;
    virtual void createRenderResource(const rhi::DevicePtr& device, const RenderParams& params, RenderGraphTable& res) = 0;
};

struct RenderGraphNode
{
    size_t index = 0;
    std::string name;
    rhi::RenderingInfo rendering;
    RenderPassType type = RP_Graphics;
    RenderGraphTable resources;
    std::unique_ptr<RenderGraphPass> pass;
    std::vector<RenderGraphNode*> edges;
};

class RenderGraph : public rhi::IRenderPass, public render::IMeshRenderer
{
  public:

    void create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain) override;
    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command) override;
    void render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const RenderParams& params) override;

  private:

    std::vector<RenderGraphNode> m_nodes;
    rhi::BindlessTablePtr m_table;

    static rhi::ResourceState guessState(const RenderNode& res);
    static void applyBarrier(rhi::CommandPtr& cmd, const RenderNode& desc, rhi::ResourceState state);

    void computeEdges(RenderGraphNode& node);
    void topologicalSort();
};

using RenderGraphPtr = std::shared_ptr<render::RenderGraph>;
} // namespace ler::render