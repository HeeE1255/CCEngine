#pragma once

#include "Core.h"
#include "json.hpp"

#include <DirectXMath.h>
#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    enum class VisualShaderNodeType
    {
        Color,
        Texture2D,
        Multiply,
        Add,
        Lerp,
        Fresnel,
        Normal,
        Roughness,
        Metallic,
        OneMinus,
        Power,
        Saturate,
        UV,
        Time,
        Output
    };

    struct VisualShaderNode
    {
        int Id = 0;
        VisualShaderNodeType Type = VisualShaderNodeType::Color;
        std::string Name;
        DirectX::XMFLOAT2 Position = { 0.0f, 0.0f };
        DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
        // 노드마다 하나씩 필요한 숫자 값을 저장한다.
        // Lerp는 섞는 비율, Fresnel/Power는 지수처럼 노드 타입에 맞춰 해석한다.
        float Value = 0.5f;
        int InputA = -1;
        int InputB = -1;
    };

    struct CC_API VisualShaderAsset
    {
        std::string Name = "New Visual Shader";
        std::vector<VisualShaderNode> Nodes;

        static VisualShaderAsset CreateDefault(const std::string& name);
        static std::filesystem::path GetGeneratedHlslPath(const std::filesystem::path& graphPath);

        bool LoadFromFile(const std::filesystem::path& path);
        bool SaveToFile(const std::filesystem::path& path) const;
        bool SaveGeneratedHlsl(const std::filesystem::path& graphPath) const;
        std::string GenerateHlslSource() const;

    private:
        static std::string NodeTypeToString(VisualShaderNodeType type);
        static VisualShaderNodeType NodeTypeFromString(const std::string& text);
    };
}
