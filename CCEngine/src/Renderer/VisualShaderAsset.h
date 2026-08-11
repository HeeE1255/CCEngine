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
        Output
    };

    struct VisualShaderNode
    {
        int Id = 0;
        VisualShaderNodeType Type = VisualShaderNodeType::Color;
        std::string Name;
        DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
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
