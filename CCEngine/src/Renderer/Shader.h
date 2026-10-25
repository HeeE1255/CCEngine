#pragma once
#include "Core.h"
#include <string>
#include <filesystem>
#include <map>
#include "Renderer/Buffer.h"
#include "Renderer/ShaderProperty.h"

namespace CCEngine
{
    class CC_API Shader
    {
    public:
        virtual ~Shader() = default;

        // 셰이더를 GPU 파이프라인에 적용
        virtual void Bind() const = 0;

        // 셰이더 해제
        virtual void Unbind() const = 0;

        virtual void BindLayout(const BufferLayout& layout) = 0;
        virtual bool IsValid() const = 0;
        using TextureBindings = std::map<std::string, uint32_t>;
        const TextureBindings& GetTextureBindings() const { return m_TextureBindings; }
        void SetTextureBindings(TextureBindings bindings);
        void SetVertexInputs(std::vector<VertexInput> inputs)
        {
            ValidateVertexInputs(inputs);
            m_VertexInputs = std::move(inputs);
            m_HasVertexInputs = true;
        }
        const std::vector<VertexInput>& GetVertexInputs() const
        {
            if (!m_HasVertexInputs) throw std::logic_error("Shader vertex input metadata is missing");
            return m_VertexInputs;
        }
        bool HasVertexInputs() const { return m_HasVertexInputs; }
        void SetMaterialPropertySource(const std::string& source)
        { m_MaterialProperties = ShaderPropertyParser::ParseSource(source); }
        const std::vector<ShaderPropertyDefinition>& GetMaterialProperties() const { return m_MaterialProperties; }

        // 파일 경로를 받아서 셰이더를 생성하는 팩토리 함수
        static Shader* Create(const std::string& filepath);
        static Shader* CreateFromBytecode(const std::filesystem::path& vertexBytecode, const std::filesystem::path& pixelBytecode);
    private:
        TextureBindings m_TextureBindings;
        std::vector<VertexInput> m_VertexInputs;
        bool m_HasVertexInputs = false;
        std::vector<ShaderPropertyDefinition> m_MaterialProperties;
    };
}
