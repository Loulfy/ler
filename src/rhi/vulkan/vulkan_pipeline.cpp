//
// Created by loulfy on 02/12/2023.
//

#include "rhi/vulkan.hpp"

#define SPIRV_REFLECT_HAS_VULKAN_H
#include <spirv_reflect.h>

#include <fstream>

namespace ler::rhi::vulkan
{
    static const std::array<std::set<std::string>, 5> c_VertexAttrMap =
    {{
         {"inPos", "in.var.POSITION"},
         {"inTex", "inUV", "in.var.TEXCOORD0"},
         {"inNormal", "in.var.NORMAL0"},
         {"inTangent", "in.var.TANGENT0"},
         {"inColor"}
     }};

    uint32_t guessVertexInputBinding(const char* name)
    {
        for (size_t i = 0; i < c_VertexAttrMap.size(); ++i)
            if (c_VertexAttrMap[i].contains(name))
                return i;
        throw std::runtime_error("Vertex Input Attribute not reserved");
    }

    vk::PrimitiveTopology convertTopology(PrimitiveType primitive)
    {
        switch(primitive)
        {

            case PrimitiveType::PointList:
                return vk::PrimitiveTopology::ePointList;
            case PrimitiveType::LineList:
                return vk::PrimitiveTopology::eLineList;
            default:
            case PrimitiveType::TriangleList:
                return vk::PrimitiveTopology::eTriangleList;
            case PrimitiveType::TriangleStrip:
                return vk::PrimitiveTopology::eTriangleStrip;
            case PrimitiveType::TriangleFan:
                return vk::PrimitiveTopology::eTriangleFan;
            case PrimitiveType::TriangleListWithAdjacency:
                return vk::PrimitiveTopology::eTriangleListWithAdjacency;
            case PrimitiveType::TriangleStripWithAdjacency:
                return vk::PrimitiveTopology::eTriangleStripWithAdjacency;
            case PrimitiveType::PatchList:
                return vk::PrimitiveTopology::ePatchList;
        }
    }

    ShaderPtr Device::createShader(const ShaderModule& shaderModule) const
    {
        fs::path path = shaderModule.path;
        path.concat(".spv");

        ShaderPtr shader = std::make_shared<Shader>();
        auto bytecode = sys::readBlobFile(path);
        vk::ShaderModuleCreateInfo shaderInfo;
        shaderInfo.setCodeSize(bytecode.size());
        shaderInfo.setPCode(reinterpret_cast<const uint32_t*>(bytecode.data()));
        shader->shaderModule = m_context.device.createShaderModuleUnique(shaderInfo);

        uint32_t count = 0;
        SpvReflectShaderModule module;
        SpvReflectResult result = spvReflectCreateShaderModule(bytecode.size(), bytecode.data(), &module);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        shader->entryPoint = module.entry_point_name;
        shader->stageFlagBits = static_cast<vk::ShaderStageFlagBits>(module.shader_stage);
        log::debug("======================================================");
        log::debug("Reflect Shader: {}, Stage: {}", path.stem().string(), vk::to_string(shader->stageFlagBits));

        // Input Variables
        result = spvReflectEnumerateInputVariables(&module, &count, nullptr);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectInterfaceVariable*> inputs(count);
        result = spvReflectEnumerateInputVariables(&module, &count, inputs.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::set<uint32_t> availableBinding;
        if (module.shader_stage == SPV_REFLECT_SHADER_STAGE_VERTEX_BIT)
        {
            for (auto& in: inputs)
            {
                if (in->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
                    continue;

                uint32_t binding = guessVertexInputBinding(in->name);
                shader->attributeDesc.emplace_back(in->location, binding, static_cast<vk::Format>(in->format), 0);
                log::debug("location = {}, binding = {}, name = {}", in->location, binding, in->name);
                if (!availableBinding.contains(binding))
                {
                    shader->bindingDesc.emplace_back(binding, 0, vk::VertexInputRate::eVertex);
                    availableBinding.insert(binding);
                }
            }

            std::sort(shader->attributeDesc.begin(), shader->attributeDesc.end(),
                      [](const VkVertexInputAttributeDescription& a, const VkVertexInputAttributeDescription& b)
                {
                  return a.location < b.location;
                });

            // Compute final offsets of each attribute, and total vertex stride.
            for (size_t i = 0; i < shader->attributeDesc.size(); ++i)
            {
                uint32_t format_size = formatSize(static_cast<VkFormat>(shader->attributeDesc[i].format));
                shader->attributeDesc[i].offset = shader->bindingDesc[i].stride;
                shader->bindingDesc[i].stride += format_size;
            }
        }

        shader->pvi = vk::PipelineVertexInputStateCreateInfo();
        shader->pvi.setVertexAttributeDescriptions(shader->attributeDesc);
        shader->pvi.setVertexBindingDescriptions(shader->bindingDesc);

        // Push Constants
        result = spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectBlockVariable*> constants(count);
        result = spvReflectEnumeratePushConstantBlocks(&module, &count, constants.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        for (auto& block: constants)
            shader->pushConstants.emplace_back(shader->stageFlagBits, block->offset, block->size);

        // Descriptor Set
        result = spvReflectEnumerateDescriptorSets(&module, &count, nullptr);
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        std::vector<SpvReflectDescriptorSet*> sets(count);
        result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());
        assert(result == SPV_REFLECT_RESULT_SUCCESS);

        for (auto& set: sets)
        {
            int useMutable = 0;
            DescriptorSetLayoutData desc;
            desc.set_number = set->set;
            desc.bindings.resize(set->binding_count);
            for (size_t i = 0; i < set->binding_count; ++i)
            {
                auto& binding = desc.bindings[i];
                binding.binding = set->bindings[i]->binding;
                binding.descriptorCount = set->bindings[i]->count;
                binding.descriptorType = static_cast<vk::DescriptorType>(set->bindings[i]->descriptor_type);
                binding.stageFlags = shader->stageFlagBits;
                log::debug("set = {}, binding = {}, count = {:02}, type = {}", set->set, binding.binding,
                           binding.descriptorCount, vk::to_string(binding.descriptorType));
                if(binding.binding == 0 && binding.descriptorCount == 0)
                    ++useMutable;
            }
            log::debug("set = {}, mutable = {}", set->set, useMutable > 1);
            shader->descriptorMap.insert({set->set, desc});
        }

        if (module.shader_stage == SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT)
        {
            // Render Target
            result = spvReflectEnumerateOutputVariables(&module, &count, nullptr);
            assert(result == SPV_REFLECT_RESULT_SUCCESS);

            std::vector<SpvReflectInterfaceVariable*> outputs(count);
            result = spvReflectEnumerateOutputVariables(&module, &count, outputs.data());
            assert(result == SPV_REFLECT_RESULT_SUCCESS);

            for(SpvReflectInterfaceVariable* var : outputs)
            {
                log::debug("texture location = {}, format = {}", var->location, vk::to_string(vk::Format(var->format)));
            }
        }

        spvReflectDestroyShaderModule(&module);
        return shader;
    }

    void BasePipeline::reflectPipelineLayout(vk::Device device, const std::span<ShaderPtr>& shaders)
    {
        // PIPELINE LAYOUT STATE
        auto layoutInfo = vk::PipelineLayoutCreateInfo();
        std::vector<vk::PushConstantRange> pushConstants;
        for (auto& shader: shaders)
            pushConstants.insert(pushConstants.end(), shader->pushConstants.begin(), shader->pushConstants.end());
        layoutInfo.setPushConstantRanges(pushConstants);

        // SHADER REFLECT
        std::set<uint32_t> sets;
        std::vector<vk::DescriptorPoolSize> descriptorPoolSizeInfo;
        std::multimap<uint32_t, DescriptorSetLayoutData> mergedDesc;
        for (auto& shader: shaders)
            mergedDesc.merge(shader->descriptorMap);

        for (auto& e: mergedDesc)
            sets.insert(e.first);

        std::vector<vk::DescriptorSetLayout> setLayouts;
        setLayouts.reserve(sets.size());
        for (auto& set: sets)
        {
            descriptorPoolSizeInfo.clear();
            auto it = descriptorAllocMap.emplace(set, DescriptorAllocator());
            auto& allocator = std::get<0>(it)->second;

            auto descriptorPoolInfo = vk::DescriptorPoolCreateInfo();
            auto descriptorLayoutInfo = vk::DescriptorSetLayoutCreateInfo();
            auto range = mergedDesc.equal_range(set);
            for (auto e = range.first; e != range.second; ++e)
                allocator.layoutBinding.insert(allocator.layoutBinding.end(), e->second.bindings.begin(),
                                               e->second.bindings.end());
            descriptorLayoutInfo.setBindings(allocator.layoutBinding);

            std::initializer_list<vk::DescriptorType> types = {vk::DescriptorType::eSampledImage, vk::DescriptorType::eStorageImage, vk::DescriptorType::eStorageBuffer};
            vk::MutableDescriptorTypeListEXT list;
            list.setDescriptorTypes(types);
            std::vector<vk::MutableDescriptorTypeListEXT> mutable_list;
            for(const auto& b : allocator.layoutBinding)
            {
                if(b.descriptorType == vk::DescriptorType::eMutableEXT)
                    mutable_list.emplace_back(list);
                else
                    mutable_list.emplace_back();
            }
            vk::MutableDescriptorTypeCreateInfoEXT mutable_info;
            mutable_info.setMutableDescriptorTypeLists(mutable_list);

            vk::DescriptorSetLayoutBindingFlagsCreateInfo extended_info;
            vk::DescriptorBindingFlags bindless_flags = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
            std::vector<vk::DescriptorBindingFlags> binding_flags(descriptorLayoutInfo.bindingCount, bindless_flags);
            extended_info.setBindingFlags(binding_flags);
            extended_info.setPNext(&mutable_info);

            static constexpr uint32_t kMaxSets = 16;
            descriptorLayoutInfo.setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool);
            descriptorLayoutInfo.setPNext(&extended_info);
            for (auto& b: allocator.layoutBinding)
                descriptorPoolSizeInfo.emplace_back(b.descriptorType, b.descriptorCount * kMaxSets);
            descriptorPoolInfo.setPoolSizes(descriptorPoolSizeInfo);
            descriptorPoolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind);
            descriptorPoolInfo.setMaxSets(kMaxSets);
            allocator.pool = device.createDescriptorPoolUnique(descriptorPoolInfo);

            vk::DescriptorSetLayoutSupport support;
            device.getDescriptorSetLayoutSupport(&descriptorLayoutInfo, &support);
            if(!support.supported)
                log::exit("Mutable DescriptorSet not supported");

            allocator.layout = device.createDescriptorSetLayoutUnique(descriptorLayoutInfo);
            setLayouts.push_back(allocator.layout.get());
        }

        setLayouts[0] = m_context.bindlessLayout;
        layoutInfo.setSetLayouts(setLayouts);
        pipelineLayout = device.createPipelineLayoutUnique(layoutInfo);
    }

    void BasePipeline::createDescriptorSet(uint32_t set)
    {
        if (!descriptorAllocMap.contains(set))
            return;

        vk::Result res;
        vk::DescriptorSet descriptorSet;
        const auto& allocator = descriptorAllocMap[set];
        vk::DescriptorSetAllocateInfo descriptorSetAllocInfo;
        descriptorSetAllocInfo.setDescriptorSetCount(1);
        descriptorSetAllocInfo.setDescriptorPool(allocator.pool.get());
        descriptorSetAllocInfo.setPSetLayouts(&allocator.layout.get());
        res = m_context.device.allocateDescriptorSets(&descriptorSetAllocInfo, &descriptorSet);
        assert(res == vk::Result::eSuccess);
        descriptorPoolMap.emplace(static_cast<VkDescriptorSet>(descriptorSet), set);

        m_descriptors.emplace_back(descriptorSet);
    }

    std::optional<vk::DescriptorType> BasePipeline::findBindingType(uint32_t set, uint32_t binding)
    {
        if(descriptorAllocMap.contains(set))
        {
            auto& descriptorDesc = descriptorAllocMap[set];
            for(const vk::DescriptorSetLayoutBinding& info : descriptorDesc.layoutBinding)
            {
                if(info.binding == binding)
                    return info.descriptorType;
            }
        }
        return std::nullopt;
    }

    void BasePipeline::updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, TexturePtr& texture)
    {
        auto* image = checked_cast<Texture*>(texture.get());
        auto* native = checked_cast<Sampler*>(sampler.get());

        auto d = m_descriptors[descriptor];
        uint32_t set = descriptorPoolMap[d];
        auto type = findBindingType(set, binding);
        if(!type.has_value())
        {
            log::error("updateSampler: set {}, binding {} is empty", set, binding);
            return;
        }
        std::vector<vk::WriteDescriptorSet> descriptorWrites;
        std::vector<vk::DescriptorImageInfo> descriptorImageInfo;

        auto descriptorWriteInfo = vk::WriteDescriptorSet();
        descriptorWriteInfo.setDescriptorType(type.value());
        descriptorWriteInfo.setDstBinding(binding);
        descriptorWriteInfo.setDstSet(d);
        descriptorWriteInfo.setDescriptorCount(1);

        auto& imageInfo = descriptorImageInfo.emplace_back();
        imageInfo = vk::DescriptorImageInfo();
        imageInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        imageInfo.setSampler(native->handle.get());
        imageInfo.setImageView(image->view());

        descriptorWriteInfo.setImageInfo(descriptorImageInfo);
        descriptorWrites.push_back(descriptorWriteInfo);
        m_context.device.updateDescriptorSets(descriptorWrites, nullptr);
    }

    void BasePipeline::updateSampler(uint32_t descriptor, uint32_t binding, SamplerPtr& sampler, const std::span<TexturePtr>& textures)
    {
        auto* native = checked_cast<Sampler*>(sampler.get());

        auto d = m_descriptors[descriptor];
        uint32_t set = descriptorPoolMap[d];
        auto type = findBindingType(set, binding);
        if(!type.has_value())
        {
            log::error("updateSampler: set {}, binding {} is empty", set, binding);
            return;
        }
        std::vector<vk::WriteDescriptorSet> descriptorWrites;
        std::vector<vk::DescriptorImageInfo> descriptorImageInfo;

        auto descriptorWriteInfo = vk::WriteDescriptorSet();
        descriptorWriteInfo.setDescriptorType(type.value());
        descriptorWriteInfo.setDstBinding(binding);
        descriptorWriteInfo.setDstSet(d);
        descriptorWriteInfo.setDescriptorCount(textures.size());

        for(auto& tex : textures)
        {
            auto* image = checked_cast<Texture*>(tex.get());
            auto& imageInfo = descriptorImageInfo.emplace_back();
            imageInfo = vk::DescriptorImageInfo();
            imageInfo.setSampler(native->handle.get());
            imageInfo.setImageLayout(vk::ImageLayout::eReadOnlyOptimal);
            if(type == vk::DescriptorType::eStorageImage)
                imageInfo.setImageLayout(vk::ImageLayout::eGeneral);
            if(tex)
                imageInfo.setImageView(image->view({vk::ImageAspectFlagBits::eColor, 0, image->info.mipLevels, 0, 1}));
        }

        descriptorWriteInfo.setImageInfo(descriptorImageInfo);
        descriptorWrites.push_back(descriptorWriteInfo);
        m_context.device.updateDescriptorSets(descriptorWrites, nullptr);
    }

    void BasePipeline::updateStorage(uint32_t descriptor, uint32_t binding, BufferPtr& buffer, uint64_t byteSize)
    {
        auto* native = checked_cast<Buffer*>(buffer.get());

        if(m_descriptors.empty())
            return;

        auto d = m_descriptors[descriptor];
        uint32_t set = descriptorPoolMap[d];
        auto type = findBindingType(set, binding);
        std::vector<vk::WriteDescriptorSet> descriptorWrites;

        auto descriptorWriteInfo = vk::WriteDescriptorSet();
        descriptorWriteInfo.setDescriptorType(type.value());
        descriptorWriteInfo.setDstBinding(binding);
        descriptorWriteInfo.setDstSet(d);
        descriptorWriteInfo.setDescriptorCount(1);

        vk::DescriptorBufferInfo buffInfo(native->handle, 0, VK_WHOLE_SIZE);

        descriptorWriteInfo.setBufferInfo(buffInfo);
        descriptorWrites.push_back(descriptorWriteInfo);
        m_context.device.updateDescriptorSets(descriptorWrites, nullptr);
    }

    void addShaderStage(std::vector<vk::PipelineShaderStageCreateInfo>& stages, const ShaderPtr& shader)
    {
        stages.emplace_back(
    vk::PipelineShaderStageCreateFlags(),
    shader->stageFlagBits,
    shader->shaderModule.get(),
    shader->entryPoint.c_str(),
    nullptr
        );
    }

    rhi::PipelinePtr Device::createGraphicsPipeline(const std::span<ShaderModule>& shaderModules, const PipelineDesc& desc)
    {
        std::vector<ShaderPtr> shaders;
        for(const ShaderModule& shaderModule : shaderModules)
            shaders.emplace_back(createShader(shaderModule));
        vk::PipelineRenderingCreateInfo renderingCreateInfo;
        std::vector<vk::Format> colorAttachments;
        colorAttachments.reserve(desc.colorAttach.size());
        for(const Format& f : desc.colorAttach)
            colorAttachments.emplace_back(convertFormat(f));
        renderingCreateInfo.setColorAttachmentFormats(colorAttachments);
        if(desc.writeDepth)
            renderingCreateInfo.setDepthAttachmentFormat(convertFormat(desc.depthAttach));

        auto pipeline = std::make_shared<GraphicsPipeline>(m_context);
        std::vector<vk::PipelineShaderStageCreateInfo> pipelineShaderStages;
        for (auto& shader: shaders)
            addShaderStage(pipelineShaderStages, shader);

        // TOPOLOGY STATE
        vk::PipelineInputAssemblyStateCreateInfo pia(vk::PipelineInputAssemblyStateCreateFlags(), convertTopology(desc.topology));

        // VIEWPORT STATE
        vk::Extent2D extent(desc.viewport.width, desc.viewport.height);
        auto viewport = vk::Viewport(0, 0, float(extent.width), float(extent.height), 0.f, 1.0f);
        auto renderArea = vk::Rect2D(vk::Offset2D(), extent);

        vk::PipelineViewportStateCreateInfo pv(vk::PipelineViewportStateCreateFlagBits(), 1, &viewport, 1, &renderArea);

        // Multi Sampling STATE
        vk::PipelineMultisampleStateCreateInfo pm(vk::PipelineMultisampleStateCreateFlags(), pickImageSample(desc.sampleCount));

        // POLYGON STATE
        vk::PipelineRasterizationStateCreateInfo pr;
        pr.setDepthClampEnable(VK_TRUE);
        pr.setRasterizerDiscardEnable(VK_FALSE);
        if(desc.fillMode == RasterFillMode::Fill)
            pr.setPolygonMode(vk::PolygonMode::eFill);
        else
            pr.setPolygonMode(vk::PolygonMode::eLine);
        pr.setFrontFace(vk::FrontFace::eCounterClockwise);
        pr.setCullMode(vk::CullModeFlagBits::eNone);
        pr.setDepthBiasEnable(VK_FALSE);
        pr.setDepthBiasConstantFactor(0.f);
        pr.setDepthBiasClamp(0.f);
        pr.setDepthBiasSlopeFactor(0.f);
        pr.setLineWidth(desc.lineWidth);

        // DEPTH & STENCIL STATE
        vk::PipelineDepthStencilStateCreateInfo pds;
        pds.setDepthTestEnable(VK_TRUE);
        pds.setDepthWriteEnable(desc.writeDepth);
        pds.setDepthCompareOp(vk::CompareOp::eLessOrEqual);
        pds.setDepthBoundsTestEnable(VK_FALSE);
        pds.setStencilTestEnable(VK_FALSE);
        pds.setFront(vk::StencilOpState());
        pds.setBack(vk::StencilOpState());
        pds.setMinDepthBounds(0.f);
        pds.setMaxDepthBounds(1.f);

        // BLEND STATE
        std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
        vk::PipelineColorBlendAttachmentState pcb;
        pcb.setBlendEnable(VK_TRUE); // false
        pcb.setSrcColorBlendFactor(vk::BlendFactor::eOne); //one //srcAlpha
        pcb.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha); //one //oneminussrcalpha
        pcb.setColorBlendOp(vk::BlendOp::eAdd);
        pcb.setSrcAlphaBlendFactor(vk::BlendFactor::eOne); //one //oneminussrcalpha
        pcb.setDstAlphaBlendFactor(vk::BlendFactor::eZero); //zero
        pcb.setAlphaBlendOp(vk::BlendOp::eAdd);
        pcb.setColorWriteMask(
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA);

        for (vk::Format& attachment: colorAttachments)
        {
            if (guessImageAspectFlags(attachment, false) == vk::ImageAspectFlagBits::eColor)
                colorBlendAttachments.push_back(pcb);
        }

        vk::PipelineColorBlendStateCreateInfo pbs;
        pbs.setLogicOpEnable(VK_FALSE);
        pbs.setLogicOp(vk::LogicOp::eClear);
        pbs.setAttachments(colorBlendAttachments);

        // DYNAMIC STATE
        std::vector<vk::DynamicState> dynamicStates =
        {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo pdy(vk::PipelineDynamicStateCreateFlags(), dynamicStates);

        // PIPELINE LAYOUT STATE
        auto layoutInfo = vk::PipelineLayoutCreateInfo();
        std::vector<vk::PushConstantRange> pushConstants;
        for (ShaderPtr& shader : shaders)
            pushConstants.insert(pushConstants.end(), shader->pushConstants.begin(), shader->pushConstants.end());
        layoutInfo.setPushConstantRanges(pushConstants);

        // SHADER REFLECT
        vk::PipelineVertexInputStateCreateInfo pvi;
        for (ShaderPtr& shader: shaders)
        {
            if (shader->stageFlagBits == vk::ShaderStageFlagBits::eVertex)
                pvi = shader->pvi;
            if (shader->stageFlagBits == vk::ShaderStageFlagBits::eFragment)
            {
                for (auto& e: shader->descriptorMap)
                {
                    for (auto& bind: e.second.bindings)
                        if (bind.descriptorCount == 0 &&
                            bind.descriptorType == vk::DescriptorType::eCombinedImageSampler)
                            bind.descriptorCount = desc.textureCount;
                }
            }
        }

        pipeline->reflectPipelineLayout(m_context.device, shaders);

        auto pipelineInfo = vk::GraphicsPipelineCreateInfo();
        pipelineInfo.setPNext(&renderingCreateInfo);
        pipelineInfo.setRenderPass(nullptr);
        pipelineInfo.setLayout(pipeline->pipelineLayout.get());
        pipelineInfo.setStages(pipelineShaderStages);
        pipelineInfo.setPVertexInputState(&pvi);
        pipelineInfo.setPInputAssemblyState(&pia);
        pipelineInfo.setPViewportState(&pv);
        pipelineInfo.setPRasterizationState(&pr);
        pipelineInfo.setPMultisampleState(&pm);
        pipelineInfo.setPDepthStencilState(&pds);
        pipelineInfo.setPColorBlendState(&pbs);
        pipelineInfo.setPDynamicState(&pdy);

        auto res = m_context.device.createGraphicsPipelineUnique(m_context.pipelineCache, pipelineInfo);
        assert(res.result == vk::Result::eSuccess);
        pipeline->handle = std::move(res.value);
        return pipeline;
    }

    rhi::PipelinePtr Device::createComputePipeline(const ShaderModule& shaderModule)
    {
        ShaderPtr shader = createShader(shaderModule);
        auto pipeline = std::make_shared<ComputePipeline>(m_context);
        std::vector<vk::PipelineShaderStageCreateInfo> pipelineShaderStages;
        addShaderStage(pipelineShaderStages, shader);

        std::vector<ShaderPtr> shaders = {shader};
        pipeline->reflectPipelineLayout(m_context.device, shaders);

        auto pipelineInfo = vk::ComputePipelineCreateInfo();
        pipelineInfo.setStage(pipelineShaderStages.front());
        pipelineInfo.setLayout(pipeline->pipelineLayout.get());

        auto res = m_context.device.createComputePipelineUnique(m_context.pipelineCache, pipelineInfo);
        pipeline->bindPoint = vk::PipelineBindPoint::eCompute;
        assert(res.result == vk::Result::eSuccess);
        pipeline->handle = std::move(res.value);
        return pipeline;
    }
}