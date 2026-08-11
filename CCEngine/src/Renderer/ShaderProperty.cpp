#include "Renderer/ShaderProperty.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace CCEngine
{
    namespace
    {
        std::string Trim(std::string value)
        {
            auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](unsigned char c) { return !isSpace(c); }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [&](unsigned char c) { return !isSpace(c); }).base(), value.end());
            return value;
        }

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        bool TryParseFloat(const std::string& text, float& outValue)
        {
            try
            {
                size_t consumed = 0;
                outValue = std::stof(Trim(text), &consumed);
                return consumed > 0;
            }
            catch (...)
            {
                return false;
            }
        }

        DirectX::XMFLOAT4 ParseColorDefault(const std::string& text)
        {
            DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
            std::stringstream stream(text);
            std::string token;
            float values[4] = { color.x, color.y, color.z, color.w };
            int index = 0;
            while (std::getline(stream, token, ',') && index < 4)
            {
                TryParseFloat(token, values[index]);
                ++index;
            }

            return { values[0], values[1], values[2], values[3] };
        }
    }

    std::vector<ShaderPropertyDefinition> ShaderPropertyParser::LoadFromShaderFile(const std::filesystem::path& shaderPath)
    {
        std::ifstream file(shaderPath);
        if (!file.is_open())
            return {};

        std::stringstream buffer;
        buffer << file.rdbuf();
        return ParseSource(buffer.str());
    }

    std::vector<ShaderPropertyDefinition> ShaderPropertyParser::ParseSource(const std::string& source)
    {
        std::vector<ShaderPropertyDefinition> properties;
        std::regex propertyPattern(R"(^\s*//\s*@property\s+([A-Za-z0-9_]+)\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*=\s*(.*))?$)");
        std::regex rangePattern(R"(range\s*\(\s*([-+]?[0-9]*\.?[0-9]+)\s*,\s*([-+]?[0-9]*\.?[0-9]+)\s*\))");

        std::stringstream stream(source);
        std::string line;
        while (std::getline(stream, line))
        {
            std::smatch match;
            if (!std::regex_match(line, match, propertyPattern))
                continue;

            ShaderPropertyDefinition definition;
            definition.Type = TypeFromString(match[1].str());
            definition.Name = match[2].str();
            definition.DisplayName = definition.Name;
            if (definition.Type == ShaderPropertyType::Unknown || definition.Name.empty())
                continue;

            std::string defaultText = match.size() >= 4 ? Trim(match[3].str()) : "";
            std::smatch rangeMatch;
            if (std::regex_search(defaultText, rangeMatch, rangePattern))
            {
                definition.HasRange = true;
                TryParseFloat(rangeMatch[1].str(), definition.Min);
                TryParseFloat(rangeMatch[2].str(), definition.Max);
                defaultText = Trim(std::regex_replace(defaultText, rangePattern, ""));
            }

            // Shader Property는 HLSL 주석에서 읽지만, 구조는 Visual Shader도 그대로 사용할 수 있게 둔다.
            // 나중에 노드 그래프는 이 Definition 목록만 내보내면 Inspector와 저장 포맷을 다시 만들 필요가 없다.
            if (!defaultText.empty())
            {
                if (definition.Type == ShaderPropertyType::Color)
                    definition.DefaultColor = ParseColorDefault(defaultText);
                else if (definition.Type == ShaderPropertyType::Float)
                    TryParseFloat(defaultText, definition.DefaultFloat);
                else if (definition.Type == ShaderPropertyType::Toggle)
                    definition.DefaultBool = ToLower(defaultText) == "true" || defaultText == "1";
                else if (definition.Type == ShaderPropertyType::Texture2D)
                    definition.DefaultTexturePath = defaultText;
            }

            properties.push_back(definition);
        }

        return properties;
    }

    ShaderPropertyType ShaderPropertyParser::TypeFromString(const std::string& text)
    {
        std::string lower = ToLower(text);
        if (lower == "float")
            return ShaderPropertyType::Float;
        if (lower == "color" || lower == "float4")
            return ShaderPropertyType::Color;
        if (lower == "texture2d" || lower == "texture")
            return ShaderPropertyType::Texture2D;
        if (lower == "toggle" || lower == "bool")
            return ShaderPropertyType::Toggle;
        return ShaderPropertyType::Unknown;
    }

    std::string ShaderPropertyParser::TypeToString(ShaderPropertyType type)
    {
        switch (type)
        {
        case ShaderPropertyType::Float: return "Float";
        case ShaderPropertyType::Color: return "Color";
        case ShaderPropertyType::Texture2D: return "Texture2D";
        case ShaderPropertyType::Toggle: return "Toggle";
        default: return "Unknown";
        }
    }
}
