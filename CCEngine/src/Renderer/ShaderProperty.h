#pragma once

#include "Core.h"

#include <DirectXMath.h>
#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    enum class ShaderPropertyType
    {
        Float = 0,
        Color,
        Texture2D,
        Toggle,
        Unknown
    };

    struct CC_API ShaderPropertyDefinition
    {
        std::string Name;
        std::string DisplayName;
        ShaderPropertyType Type = ShaderPropertyType::Unknown;
        DirectX::XMFLOAT4 DefaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float DefaultFloat = 0.0f;
        float Min = 0.0f;
        float Max = 1.0f;
        bool HasRange = false;
        bool DefaultBool = false;
        std::string DefaultTexturePath;
    };

    struct CC_API ShaderPropertyValue
    {
        ShaderPropertyType Type = ShaderPropertyType::Unknown;
        DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float FloatValue = 0.0f;
        bool BoolValue = false;
        std::string TextureGuid;
        std::string TexturePath;
    };

    class CC_API ShaderPropertyParser
    {
    public:
        static std::vector<ShaderPropertyDefinition> LoadFromShaderFile(const std::filesystem::path& shaderPath);
        static std::vector<ShaderPropertyDefinition> ParseSource(const std::string& source);
        static ShaderPropertyType TypeFromString(const std::string& text);
        static std::string TypeToString(ShaderPropertyType type);
    };
}
