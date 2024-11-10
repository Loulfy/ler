//
// Created by loulfy on 09/03/2024.
//

#include "graph.hpp"
#include <stack>

namespace ler::render
{
/*
void to_json(json& j, const RenderDesc& r)
{
}

void from_json(const json& j, RenderDesc& r)
{
    r.name = j["name"];
    r.type = j["type"];
    r.binding = j["binding"];

    if (r.type == RR_StorageBuffer)
    {
        // r.buffer.byteSize = j["byteSize"];
        // r.buffer.usage = vk::BufferUsageFlags(j["usage"].get<uint32_t>());
    }
    else if (r.type == RR_RenderTarget || r.type == RR_DepthWrite || r.type == RR_StorageImage)
    {
        // r.texture.format = rhi::stringToFormat(j["format"]);
        // r.texture.loadOp = j["loadOp"];
    }
}

void RenderGraph::parse(const fs::path& path)
{
    std::vector<char> bin = sys::readBlobFile(sys::ASSETS_DIR / path);
    try
    {
        m_info = json::parse(bin);
    }
    catch (const std::exception& e)
    {
        log::error("Failed to parse RenderGraph: {}", path.string());
    }

    for (size_t i = 0; i < m_info.passes.size(); ++i)
    {
        RenderPassDesc& pass = m_info.passes[i];
        RenderGraphNode& node = m_nodes.emplace_back();
        node.index = i;
        node.type = pass.pass;
        node.name = pass.name;
        for (RenderDesc& desc : pass.resources)
        {
            if (m_resourceMap.contains(desc.name))
            {
                desc.handle = m_resourceMap.at(desc.name);
                node.bindings.emplace_back(desc);
            }
            else
            {
                // log::error("[RenderPass] {} can not found input: {}", pass.name, ref.name);
                desc.handle = m_resourceCache.size();
                node.bindings.emplace_back(desc);
                m_resourceCache.emplace_back();
                m_resourceMap.emplace(desc.name, desc.handle);
            }

            if (guessOutput(desc))
                node.outputs.insert(desc.handle);
        }
    }

    for (RenderGraphNode& node : m_nodes)
        computeEdges(node);

    topologicalSort();
    for (RenderGraphNode& node : m_nodes)
        log::info("[RenderGraph] Create Node: {}", node.name);
}

void RenderGraph::compile(const rhi::DevicePtr& device)
{
    log::debug("[RenderGraph] Compile");

    samplerGlobal = device->createSampler(
        { true, rhi::SamplerAddressMode::Repeat, rhi::SamplerAddressMode::Repeat, rhi::SamplerAddressMode::Repeat });
    // samplerGlobal = device->createSampler({true});
    m_table = device->createBindlessTable(128);

    for (RenderGraphNode& node : m_nodes)
    {
        if (node.pass == nullptr)
        {
            log::error("[RenderGraph] Node: {} Not Implemented", node.name);
            continue;
            // throw std::runtime_error("RenderGraph Node Not Implemented");
        }

        for (RenderDesc& desc : node.bindings)
        {
            if (desc.type == RR_StorageBuffer || desc.type == RR_ConstantBuffer)
            {
                desc.buffer.debugName = desc.name;
                node.pass->getResourceDesc(desc);
                log::debug("[RenderGraph] Add buf: {:10s}", desc.name);
                m_resourceCache[desc.handle] = device->createBuffer(desc.buffer);
            }
        }

        for (RenderDesc& res : node.bindings)
            bindResource(nullptr, res);

        node.pass->create(device, *this, node.bindings);
        rhi::PipelinePtr pipeline = node.pass->getPipeline();
        if (pipeline == nullptr)
            continue;
        //pipeline->createDescriptorSet(0);
        //for (RenderDesc& res : node.bindings)
            //bindResource(pipeline, res);
    }
}

void RenderGraph::setRenderAttachment(const RenderDesc& desc, rhi::Attachment& info)
{
    RenderResource& r = m_resourceCache[desc.handle];
    if (std::holds_alternative<rhi::TexturePtr>(r))
    {
        info.texture = std::get<rhi::TexturePtr>(r);
        info.loadOp = rhi::AttachmentLoadOp::Clear;
    }
}

void RenderGraph::resize(const rhi::DevicePtr& device, const rhi::Extent& viewport)
{
    log::debug("[RenderGraph] Resize");
    for (RenderGraphNode& node : m_nodes)
    {
        for (RenderDesc& desc : node.bindings)
        {
            if (desc.type == RR_RenderTarget || desc.type == RR_DepthWrite || desc.type == RR_StorageImage)
            {
                desc.texture.width = viewport.width;
                desc.texture.height = viewport.height;
                node.pass->getResourceDesc(desc);
                log::debug("[RenderGraph] Add tex: {:10s} -> {}", desc.name, rhi::to_string(desc.texture.format));
                m_resourceCache[desc.handle] = device->createTexture(desc.texture);
            }
        }

        if (node.pass == nullptr)
            continue;
        node.pass->resize(device, viewport);
        node.rendering.viewport = viewport;
        node.rendering.colorCount = 0;
        rhi::Attachment* info;
        rhi::PipelinePtr pipeline = node.pass->getPipeline();
        for (const RenderDesc& desc : node.bindings)
        {
            switch (desc.type)
            {
            case RR_RenderTarget:
                node.rendering.colorCount += 1;
                info = &node.rendering.colors[desc.binding];
                setRenderAttachment(desc, *info);
                break;
            case RR_DepthWrite:
                info = &node.rendering.depth;
                setRenderAttachment(desc, *info);
                break;
            default:
                break;
            }

            //if (pipeline)
                //bindResource(pipeline, desc);
        }
    }
}

void RenderGraph::execute(rhi::CommandPtr& cmd, rhi::TexturePtr& backBuffer, const RenderParams& params)
{
    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        RenderGraphNode& node = m_nodes[i];
        auto& color = rhi::Color::Palette[i];

        // Begin Pass
        cmd->beginDebugEvent(node.name, color);
        node.rendering.viewport = backBuffer->extent();

        // Apply Barrier
        for (RenderDesc& desc : node.bindings)
        {
            rhi::ResourceState state = guessState(desc);
            applyBarrier(cmd, desc, state);

            if (desc.name == "color" && node.type == RP_Graphics)
                node.rendering.colors[desc.binding].texture = backBuffer;
        }

        // Begin Rendering
        if (node.type == RP_Graphics)
            cmd->beginRendering(node.rendering);

        // Render
        if (node.pass)
        {
            if (node.pass->getPipeline())
                cmd->bindPipeline(node.pass->getPipeline(), m_table);
            node.pass->render(cmd, params);
        }

        // End Pass
        if (node.type == RP_Graphics)
            cmd->endRendering();
        cmd->endDebugEvent();
    }
}

void RenderGraph::rebind()
{
    if (m_nodes.empty())
        return;
    auto& node = m_nodes.back();
    bindResource(node.pass->getPipeline(), node.bindings[5]);
}

void RenderGraph::addResource(const std::string& name, const RenderResource& res)
{
    if (m_resourceMap.contains(name))
    {
        uint32_t handle = m_resourceMap.at(name);
        m_resourceCache[handle] = res;
    }
    else
    {
        m_resourceMap.emplace(name, uint32_t(m_resourceCache.size()));
        m_resourceCache.emplace_back(res);
    }
}

bool RenderGraph::getResource(uint32_t handle, rhi::BufferPtr& buffer) const
{
    if (m_resourceCache.size() < handle)
        return false;

    if (!std::holds_alternative<rhi::BufferPtr>(m_resourceCache[handle]))
        return false;

    buffer = std::get<rhi::BufferPtr>(m_resourceCache[handle]);
    return true;
}

void RenderGraph::applyBarrier(rhi::CommandPtr& cmd, const RenderDesc& desc, rhi::ResourceState state)
{
    RenderResource& r = m_resourceCache[desc.handle];
    if (std::holds_alternative<rhi::BufferPtr>(r))
    {
        rhi::BufferPtr buf = std::get<rhi::BufferPtr>(r);
        cmd->addBufferBarrier(buf, state);
    }
    else if (std::holds_alternative<rhi::TexturePtr>(r))
    {
        rhi::TexturePtr tex = std::get<rhi::TexturePtr>(r);
        cmd->addImageBarrier(tex, state);
    }
}

void RenderGraph::bindResource(const rhi::PipelinePtr& pipeline, RenderDesc& res)
{
    if (res.handle == UINT32_MAX || res.binding == UINT32_MAX)
        return;

    RenderResource& r = m_resourceCache[res.handle];
    if (std::holds_alternative<rhi::BufferPtr>(r))
    {
        auto& buf = std::get<rhi::BufferPtr>(r);
        res.bindlessIndex = m_table->allocate();
        m_table->setResource(buf, res.bindlessIndex);
    }
    else if (std::holds_alternative<rhi::TexturePtr>(r))
    {
        auto& tex = std::get<rhi::TexturePtr>(r);
        res.bindlessIndex = m_table->allocate();
        m_table->setResource(tex, res.bindlessIndex);
    }
}

bool RenderGraph::guessOutput(const RenderDesc& res)
{
    switch (res.type)
    {
    default:
        return false;
    case RR_DepthWrite:
    case RR_RenderTarget:
    case RR_StorageImage:
    case RR_StorageBuffer:
    case RR_ConstantBuffer:
        return true;
    }
}

rhi::ResourceState RenderGraph::guessState(const RenderDesc& res)
{
    switch (res.type)
    {
    case RR_DepthWrite:
        return rhi::DepthWrite;
    case RR_RenderTarget:
        return rhi::RenderTarget;
    case RR_SampledTexture:
        return rhi::ShaderResource;
    case RR_ReadOnlyBuffer:
        return rhi::Indirect;
    case RR_ConstantBuffer:
        return rhi::ConstantBuffer;
    case RR_StorageImage:
    case RR_StorageBuffer:
        return rhi::UnorderedAccess;
    default:
        return rhi::Undefined;
    }
}

void RenderGraph::computeEdges(RenderGraphNode& node)
{
    for (const RenderDesc& desc : node.bindings)
    {
        if (guessOutput(desc))
            continue;

        for (auto& parent : m_nodes)
        {
            if (parent.outputs.contains(desc.handle) &&
                std::find(parent.edges.begin(), parent.edges.end(), &node) == parent.edges.end())
            {
                parent.edges.emplace_back(&node);
                break;
            }
        }
    }
}

void RenderGraph::topologicalSort()
{
    enum Status
    {
        New = 0u,
        Visited,
        Added
    };

    std::stack<RenderGraphNode*> stack;
    std::vector<uint8_t> node_status(m_nodes.size(), New);

    std::vector<RenderGraphNode> sp = std::move(m_nodes);

    // Topological sorting
    for (auto& node : sp)
    {
        stack.push(&node);

        while (!stack.empty())
        {
            RenderGraphNode* node_handle = stack.top();

            if (node_status[node_handle->index] == Added)
            {
                stack.pop();
                continue;
            }

            if (node_status[node_handle->index] == Visited)
            {
                node_status[node_handle->index] = Added;
                m_nodes.emplace_back(std::move(*node_handle));
                stack.pop();
                continue;
            }

            node_status[node_handle->index] = Visited;

            // Leaf node
            for (auto child_handle : node_handle->edges)
            {
                if (node_status[child_handle->index] == New)
                    stack.push(child_handle);
            }
        }
    }

    // Reverse
    std::reverse(m_nodes.begin(), m_nodes.end());
}

void RenderGraph::create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain)
{
    compile(device);
}

void RenderGraph::render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command)
{
    // execute(command, backBuffer, m_meshList, params);
}

void RenderGraph::render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const RenderParams& params)
{
    execute(command, backBuffer, params);
}*/

rhi::BufferPtr getBufferOutput(const RenderGraphTable& res, uint32_t id)
{
    assert(res.outputs.size() > id);
    assert(std::holds_alternative<rhi::BufferPtr>(res.outputs[id].resource));
    return std::get<rhi::BufferPtr>(res.outputs[id].resource);
}

void RenderGraph::create(const rhi::DevicePtr& device, const rhi::SwapChainPtr& swapChain)
{
}

void RenderGraph::render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command)
{
}

rhi::ResourceState RenderGraph::guessState(const RenderNode& res)
{
    switch (res.type)
    {
    case RR_DepthWrite:
        return rhi::DepthWrite;
    case RR_RenderTarget:
        return rhi::RenderTarget;
    case RR_SampledTexture:
        return rhi::ShaderResource;
    case RR_ReadOnlyBuffer:
        return rhi::Indirect;
    case RR_ConstantBuffer:
        return rhi::ConstantBuffer;
    case RR_StorageImage:
    case RR_StorageBuffer:
        return rhi::UnorderedAccess;
    default:
        return rhi::Undefined;
    }
}

void RenderGraph::applyBarrier(rhi::CommandPtr& cmd, const RenderNode& desc, rhi::ResourceState state)
{
    const RenderResource& r = desc.resource;
    if (std::holds_alternative<rhi::BufferPtr>(r))
    {
        rhi::BufferPtr buf = std::get<rhi::BufferPtr>(r);
        cmd->addBufferBarrier(buf, state);
    }
    else if (std::holds_alternative<rhi::TexturePtr>(r))
    {
        rhi::TexturePtr tex = std::get<rhi::TexturePtr>(r);
        cmd->addImageBarrier(tex, state);
    }
}

void RenderGraph::render(rhi::TexturePtr& backBuffer, rhi::CommandPtr& command, const RenderParams& params)
{
    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        RenderGraphNode& node = m_nodes[i];
        auto& color = rhi::Color::Palette[i];

        // Begin Pass
        command->beginDebugEvent(node.name, color);
        node.rendering.viewport = backBuffer->extent();

        // Apply Barrier
        for (RenderNode& desc : node.resources.outputs)
        {
            rhi::ResourceState state = guessState(desc);
            applyBarrier(command, desc, state);
        }

        for (RenderNode& desc : node.resources.inputs)
        {
            rhi::ResourceState state = guessState(desc);
            applyBarrier(command, desc, state);
        }

        // Begin Rendering
        if (node.type == RP_Graphics)
            command->beginRendering(node.rendering);

        // Render
        if (node.pass)
        {
            if (node.pass->getPipeline())
                command->bindPipeline(node.pass->getPipeline(), m_table);
            node.pass->render(command, params, node.resources);
        }

        // End Pass
        if (node.type == RP_Graphics)
            command->endRendering();
        command->endDebugEvent();
    }
}

static bool isRoot(RenderGraphNode& root, const RenderNode& renderNode)
{
    for (auto& in : root.resources.inputs)
    {
        if (in.resource == renderNode.resource)
            return true;
    }
    return false;
}

void RenderGraph::computeEdges(RenderGraphNode& node)
{
    for (const RenderNode& renderNode : node.resources.inputs)
    {
        // if (guessOutput(desc))
        // continue;

        for (auto& parent : m_nodes)
        {
            if (isRoot(parent, renderNode) &&
                std::find(parent.edges.begin(), parent.edges.end(), &node) == parent.edges.end())
            {
                parent.edges.emplace_back(&node);
                break;
            }
        }
    }
}

void RenderGraph::topologicalSort()
{
    enum Status
    {
        New = 0u,
        Visited,
        Added
    };

    std::stack<RenderGraphNode*> stack;
    std::vector<uint8_t> node_status(m_nodes.size(), New);

    std::vector<RenderGraphNode> sp = std::move(m_nodes);

    // Topological sorting
    for (auto& node : sp)
    {
        stack.push(&node);

        while (!stack.empty())
        {
            RenderGraphNode* node_handle = stack.top();

            if (node_status[node_handle->index] == Added)
            {
                stack.pop();
                continue;
            }

            if (node_status[node_handle->index] == Visited)
            {
                node_status[node_handle->index] = Added;
                m_nodes.emplace_back(std::move(*node_handle));
                stack.pop();
                continue;
            }

            node_status[node_handle->index] = Visited;

            // Leaf node
            for (auto child_handle : node_handle->edges)
            {
                if (node_status[child_handle->index] == New)
                    stack.push(child_handle);
            }
        }
    }

    // Reverse
    std::reverse(m_nodes.begin(), m_nodes.end());
}
} // namespace ler::render