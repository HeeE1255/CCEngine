#include "Renderer/VisualShaderAsset.h"

#include <fstream>
#include <sstream>

namespace CCEngine
{
    VisualShaderAsset VisualShaderAsset::CreateDefault(const std::string& name)
    {
        VisualShaderAsset asset;
        asset.Name = name.empty() ? "New Visual Shader" : name;

        VisualShaderNode color;
        color.Id = 1;
        color.Type = VisualShaderNodeType::Color;
        color.Name = "Base Color";
        color.Color = { 0.8f, 0.8f, 0.85f, 1.0f };

        VisualShaderNode texture;
        texture.Id = 2;
        texture.Type = VisualShaderNodeType::Texture2D;
        texture.Name = "Albedo Texture";

        VisualShaderNode multiply;
        multiply.Id = 3;
        multiply.Type = VisualShaderNodeType::Multiply;
        multiply.Name = "Texture x Color";
        multiply.InputA = texture.Id;
        multiply.InputB = color.Id;

        VisualShaderNode output;
        output.Id = 4;
        output.Type = VisualShaderNodeType::Output;
        output.Name = "Output";
        output.InputA = multiply.Id;

        asset.Nodes = { color, texture, multiply, output };
        return asset;
    }

    std::filesystem::path VisualShaderAsset::GetGeneratedHlslPath(const std::filesystem::path& graphPath)
    {
        return graphPath.parent_path() / (graphPath.stem().string() + ".generated.hlsl");
    }

    bool VisualShaderAsset::LoadFromFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return false;

        nlohmann::json root;
        file >> root;
        const nlohmann::json& data = root.contains("VisualShader") ? root["VisualShader"] : root;

        Name = data.value("Name", path.stem().string());
        Nodes.clear();
        if (data.contains("Nodes") && data["Nodes"].is_array())
        {
            for (const nlohmann::json& nodeData : data["Nodes"])
            {
                VisualShaderNode node;
                node.Id = nodeData.value("Id", 0);
                node.Type = NodeTypeFromString(nodeData.value("Type", "Color"));
                node.Name = nodeData.value("Name", "");
                if (nodeData.contains("Color") && nodeData["Color"].is_array() && nodeData["Color"].size() >= 4)
                {
                    node.Color = {
                        nodeData["Color"][0].get<float>(),
                        nodeData["Color"][1].get<float>(),
                        nodeData["Color"][2].get<float>(),
                        nodeData["Color"][3].get<float>()
                    };
                }
                node.InputA = nodeData.value("InputA", -1);
                node.InputB = nodeData.value("InputB", -1);
                Nodes.push_back(node);
            }
        }
        return true;
    }

    bool VisualShaderAsset::SaveToFile(const std::filesystem::path& path) const
    {
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());

        nlohmann::json root;
        auto& data = root["VisualShader"];
        data["Name"] = Name;

        for (const VisualShaderNode& node : Nodes)
        {
            nlohmann::json nodeData;
            nodeData["Id"] = node.Id;
            nodeData["Type"] = NodeTypeToString(node.Type);
            nodeData["Name"] = node.Name;
            nodeData["Color"] = { node.Color.x, node.Color.y, node.Color.z, node.Color.w };
            nodeData["InputA"] = node.InputA;
            nodeData["InputB"] = node.InputB;
            data["Nodes"].push_back(nodeData);
        }

        std::ofstream file(path);
        if (!file.is_open())
            return false;

        file << root.dump(4);
        return true;
    }

    bool VisualShaderAsset::SaveGeneratedHlsl(const std::filesystem::path& graphPath) const
    {
        std::filesystem::path hlslPath = GetGeneratedHlslPath(graphPath);
        if (!hlslPath.parent_path().empty())
            std::filesystem::create_directories(hlslPath.parent_path());

        std::ofstream file(hlslPath);
        if (!file.is_open())
            return false;

        file << GenerateHlslSource();
        return true;
    }

    std::string VisualShaderAsset::GenerateHlslSource() const
    {
        // 그래프 원본은 JSON으로 저장하지만, 런타임은 기존 HLSL 컴파일 경로를 그대로 사용한다.
        // 노드 시스템을 키워도 최종 산출물이 같은 형식이면 Material/Reflection/캐시 코드를 다시 만들 필요가 없다.
        DirectX::XMFLOAT4 color = { 0.8f, 0.8f, 0.85f, 1.0f };
        for (const VisualShaderNode& node : Nodes)
        {
            if (node.Type == VisualShaderNodeType::Color)
            {
                color = node.Color;
                break;
            }
        }

        std::ostringstream source;
        source
            << "// Generated from Visual Shader: " << Name << "\n"
            << "// @property Color AlbedoColor = " << color.x << "," << color.y << "," << color.z << "," << color.w << "\n"
            << "// @property Texture2D AlbedoTexture\n"
            << "cbuffer CameraBuffer : register(b0)\n"
            << "{\n"
            << "    matrix ViewProjection;\n"
            << "};\n\n"
            << "cbuffer ObjectBuffer : register(b1)\n"
            << "{\n"
            << "    matrix Transform;\n"
            << "};\n\n"
            << "cbuffer MaterialPropertyBuffer : register(b4)\n"
            << "{\n"
            << "    float4 AlbedoColor;\n"
            << "    float4 PropertyColors[8];\n"
            << "    float4 PropertyScalars[4];\n"
            << "    float4 PropertyToggles[4];\n"
            << "    float4 SurfaceValues;\n"
            << "};\n\n"
            << "Texture2D AlbedoTexture : register(t0);\n"
            << "SamplerState LinearSampler : register(s0);\n\n"
            << "struct VSInput\n"
            << "{\n"
            << "    float3 Position : POSITION;\n"
            << "    float3 Normal : NORMAL;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "struct PSInput\n"
            << "{\n"
            << "    float4 Position : SV_POSITION;\n"
            << "    float3 Normal : NORMAL;\n"
            << "    float2 TexCoord : TEXCOORD0;\n"
            << "};\n\n"
            << "PSInput VSMain(VSInput input)\n"
            << "{\n"
            << "    PSInput output;\n"
            << "    float4 worldPosition = mul(float4(input.Position, 1.0f), Transform);\n"
            << "    output.Position = mul(worldPosition, ViewProjection);\n"
            << "    output.Normal = input.Normal;\n"
            << "    output.TexCoord = input.TexCoord;\n"
            << "    return output;\n"
            << "}\n\n"
            << "float4 PSMain(PSInput input) : SV_TARGET\n"
            << "{\n"
            << "    float4 textureColor = AlbedoTexture.Sample(LinearSampler, input.TexCoord);\n"
            << "    return textureColor * AlbedoColor;\n"
            << "}\n";
        return source.str();
    }

    std::string VisualShaderAsset::NodeTypeToString(VisualShaderNodeType type)
    {
        switch (type)
        {
            case VisualShaderNodeType::Color: return "Color";
            case VisualShaderNodeType::Texture2D: return "Texture2D";
            case VisualShaderNodeType::Multiply: return "Multiply";
            case VisualShaderNodeType::Output: return "Output";
            default: return "Color";
        }
    }

    VisualShaderNodeType VisualShaderAsset::NodeTypeFromString(const std::string& text)
    {
        if (text == "Texture2D") return VisualShaderNodeType::Texture2D;
        if (text == "Multiply") return VisualShaderNodeType::Multiply;
        if (text == "Output") return VisualShaderNodeType::Output;
        return VisualShaderNodeType::Color;
    }
}
