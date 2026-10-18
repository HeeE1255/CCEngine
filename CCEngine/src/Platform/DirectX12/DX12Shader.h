#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Context.h"
#include "Renderer/Shader.h"
namespace CCEngine
{
    class DX12Shader final : public Shader
    {
    public:
        DX12Shader(DX12Context& context, const std::filesystem::path& hlsl,
            const std::string& vertexEntry = "VSMain", const std::string& pixelEntry = "PSMain");
        DX12Shader(DX12Context& context, std::vector<uint8_t> vertexBytecode, std::vector<uint8_t> pixelBytecode);
        static std::unique_ptr<DX12Shader> FromBytecodeFiles(DX12Context& context,
            const std::filesystem::path& vertex, const std::filesystem::path& pixel);
        DX12Shader(const DX12Shader&) = delete;
        DX12Shader& operator=(const DX12Shader&) = delete;
        void Bind() const override { DX12Context::CurrentFor(m_Device).BindProgram(m_Program); }
        void Unbind() const override { DX12Context::CurrentFor(m_Device).BindProgram(nullptr); }
        void BindLayout(const BufferLayout& layout) override { DX12Context::CurrentFor(m_Device).SetLayout(layout); }
        bool IsValid() const override { return m_Program && !m_Program->Vertex.empty() && !m_Program->Pixel.empty(); }
    private:
        std::shared_ptr<DX12Device> m_Device;
        std::shared_ptr<DX12Program> m_Program;
    };
}
#endif
