#include "VulkanResources.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include <sstream>
#include <stdexcept>

namespace CCEngine
{
    VkDescriptorSet VulkanContext::AllocateSet()
    {
        VkDescriptorSetAllocateInfo allocation{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocation.descriptorSetCount = 1; allocation.pSetLayouts = &m_SetLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        for (auto pool : m_DescriptorPools)
        {
            allocation.descriptorPool = pool;
            const auto result = vkAllocateDescriptorSets(m_Device, &allocation, &set);
            if (result == VK_SUCCESS) return set;
            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
                CheckVulkan(result, "Allocate descriptor snapshot");
        }
        constexpr uint32_t draws = 256;
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8 * draws },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_TextureSlots * draws },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 2 * draws }
        };
        VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        info.maxSets = draws; info.poolSizeCount = 3; info.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        CheckVulkan(vkCreateDescriptorPool(m_Device, &info, nullptr, &pool), "Create descriptor pool");
        try { m_DescriptorPools.push_back(pool); }
        catch (...) { vkDestroyDescriptorPool(m_Device, pool, nullptr); throw; }
        allocation.descriptorPool = pool;
        CheckVulkan(vkAllocateDescriptorSets(m_Device, &allocation, &set), "Allocate descriptor snapshot");
        return set;
    }

    VulkanDrawBinding VulkanContext::PrepareDraw(VkCommandBuffer command, const VulkanRasterState& state,
        const std::vector<VkDescriptorImageInfo>& images)
    {
        if (!m_InPass || !m_Target || !m_Shader || !m_Shader->Vertex || !m_Shader->Fragment || !m_Vertex || !m_Index || !m_IndexCount)
            throw std::logic_error("Vulkan draw requires a target, shader, vertex and index buffer");
        if (images.size() != m_TextureSlots) throw std::invalid_argument("Wrong descriptor texture count");
        for (const auto& image : images)
            for (const auto& color : m_Target->Colors)
                if (image.imageView == color->View) throw std::logic_error("Cannot sample the active render target");
        const auto& limits = m_Properties.limits;
        const uint32_t stride = m_VertexLayout.GetStride();
        if (!stride || stride > limits.maxVertexInputBindingStride || stride > m_Vertex->Size)
            throw std::invalid_argument("Invalid Vulkan vertex stride");
        std::ostringstream key;
        key << m_Target->ID << '/' << m_Shader->ID << '/' << int(state.Blend) << '/' << int(state.Cull)
            << '/' << state.DepthTest << '/' << stride;
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (const auto& element : ResolveVertexInputs(m_VertexLayout, m_ShaderInputs))
        {
            uint32_t columns = 1, components = 0;
            bool integer = false;
            switch (element.Type)
            {
            case ShaderDataType::Float: components = 1; break;
            case ShaderDataType::Float2: components = 2; break;
            case ShaderDataType::Float3: components = 3; break;
            case ShaderDataType::Float4: components = 4; break;
            case ShaderDataType::Mat3: components = columns = 3; break;
            case ShaderDataType::Mat4: components = columns = 4; break;
            case ShaderDataType::Int: components = 1; integer = true; break;
            case ShaderDataType::Int2: components = 2; integer = true; break;
            case ShaderDataType::Int3: components = 3; integer = true; break;
            case ShaderDataType::Int4: components = 4; integer = true; break;
            default: throw std::invalid_argument("Bool/None are not Vulkan vertex formats");
            }
            if (element.Offset > stride || components * columns * 4 > stride - element.Offset)
                throw std::invalid_argument("Vertex attribute exceeds stride");
            const VkFormat floats[] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
            const VkFormat ints[] = { VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT };
            const auto format = integer ? ints[components - 1] : floats[components - 1];
            VkFormatProperties support{}; vkGetPhysicalDeviceFormatProperties(m_Physical, format, &support);
            if (!(support.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) throw std::runtime_error("Unsupported vertex format");
            for (uint32_t column = 0; column < columns; ++column)
            {
                const uint32_t offset = element.Offset + column * components * 4;
                if (element.Location >= limits.maxVertexInputAttributes || offset > limits.maxVertexInputAttributeOffset)
                    throw std::out_of_range("Vertex layout exceeds device limits");
                attributes.push_back({ element.Location, 0, format, offset });
            }
            key << '/' << int(element.Type) << ':' << element.Offset << ':' << element.Location;
        }
        VkPipeline pipeline = VK_NULL_HANDLE;
        const auto cached = m_Pipelines.find(key.str());
        if (cached != m_Pipelines.end()) pipeline = cached->second;
        else
        {
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = m_Shader->Vertex; stages[0].pName = m_Shader->VertexEntry.c_str();
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = m_Shader->Fragment; stages[1].pName = m_Shader->FragmentEntry.c_str();
            VkVertexInputBindingDescription binding{ 0, stride, VK_VERTEX_INPUT_RATE_VERTEX };
            VkPipelineVertexInputStateCreateInfo vertex{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vertex.vertexBindingDescriptionCount = 1; vertex.pVertexBindingDescriptions = &binding;
            vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()); vertex.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
            viewport.viewportCount = viewport.scissorCount = 1;
            auto raster = VulkanRendererAPI::Rasterization(state);
            auto depth = VulkanRendererAPI::DepthStencil(state);
            VkPipelineMultisampleStateCreateInfo samples{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
            samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            auto attachments = VulkanRendererAPI::ColorBlend(state, static_cast<uint32_t>(m_Target->Colors.size()));
            VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            blend.attachmentCount = static_cast<uint32_t>(attachments.size()); blend.pAttachments = attachments.data();
            VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
            dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamics;
            VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
            info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &samples;
            info.pDepthStencilState = &depth; info.pColorBlendState = &blend; info.pDynamicState = &dynamic;
            info.layout = m_PipelineLayout; info.renderPass = m_Target->Pass;
            CheckVulkan(vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "Create graphics pipeline");
            try { m_Pipelines.emplace(key.str(), pipeline); }
            catch (...) { vkDestroyPipeline(m_Device, pipeline, nullptr); throw; }
        }
        const auto set = AllocateSet();
        VkDescriptorBufferInfo buffers[8]{};
        VkWriteDescriptorSet writes[42]{};
        for (uint32_t i = 0; i < 8; ++i)
        {
            const auto& buffer = m_Uniforms[i] ? m_Uniforms[i] : m_EmptyUniform;
            buffers[i] = { buffer->Buffer, 0, buffer->Size };
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; writes[i].pBufferInfo = &buffers[i];
        }
        // DXC에서 리소스 배열을 펼쳐 tN을 binding 8+N으로 만든다. 각 draw는 같은 슬롯 배치를 사용한다.
        for (uint32_t i = 0; i < 32; ++i)
        {
            auto& write = writes[8 + i]; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set; write.dstBinding = 8 + i; write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            write.descriptorCount = 1; write.pImageInfo = &images[i];
        }
        VkDescriptorImageInfo samplers[2]{};
        samplers[0].sampler = m_LinearSampler; samplers[1].sampler = m_Sampler;
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto& write = writes[40 + i]; write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = set; write.dstBinding = 40 + i; write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            write.descriptorCount = 1; write.pImageInfo = &samplers[i];
        }
        vkUpdateDescriptorSets(m_Device, 42, writes, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command, 0, 1, &m_Vertex->Buffer, &offset);
        vkCmdBindIndexBuffer(command, m_Index->Buffer, 0, VK_INDEX_TYPE_UINT32);
        VulkanDrawBinding result; result.Pipeline = pipeline; result.Layout = m_PipelineLayout;
        result.DescriptorSets.push_back(set);
        return result;
    }
}
#endif
