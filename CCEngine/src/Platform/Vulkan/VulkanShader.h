#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Renderer/Shader.h"
#include "VulkanResources.h"
namespace CCEngine
{
    class VulkanShader final : public Shader
    {
    public:
        // cc-hlsl-rhi-v1: set 0의 b=0~7, t=8~39, s=40~41. 리소스 배열은 DXC에서 펼친다.
        // 기본 entry는 변환 도구가 지정한 main이며 기존 결합 sampler 배열 SPIR-V와는 호환되지 않는다.
        VulkanShader(VulkanContext& context, const std::vector<uint32_t>& vertex, const std::vector<uint32_t>& fragment,
            const std::string& vertexEntry = "main", const std::string& fragmentEntry = "main");
        VulkanShader(VulkanContext& context, const std::filesystem::path& vertex, const std::filesystem::path& fragment,
            const std::string& vertexEntry = "main", const std::string& fragmentEntry = "main");
        VulkanShader(const VulkanShader&) = delete;
        VulkanShader& operator=(const VulkanShader&) = delete;
        void Bind() const override { Context().SetShaderInputs(GetVertexInputs()); Context().BindShader(m_Program); }
        void Unbind() const override { Context().BindShader(nullptr); }
        void BindLayout(const BufferLayout& layout) override { Context().SetVertexLayout(layout); }
        bool IsValid() const override { return m_Program && m_Program->Vertex && m_Program->Fragment; }
    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VulkanContext& Context() const { return VulkanContext::CurrentFor(m_Device); }
        std::shared_ptr<VulkanShaderProgram> m_Program;
    };
}
#endif
