#include "Renderer/VisualShaderAsset.h"

#include <fstream>
#include <functional>
#include <algorithm>
#include <iomanip>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

namespace CCEngine
{
    namespace
    {
        const VisualShaderNode* FindNode(const std::unordered_map<int, const VisualShaderNode*>& nodesById, int id)
        {
            auto it = nodesById.find(id);
            return it == nodesById.end() ? nullptr : it->second;
        }

        float GetDefaultNodeValue(VisualShaderNodeType type)
        {
            switch (type)
            {
                case VisualShaderNodeType::Fresnel: return 4.0f;
                case VisualShaderNodeType::Power: return 2.0f;
                default: return 0.5f;
            }
        }

        std::string FloatLiteral(float value)
        {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(3) << value << "f";
            return stream.str();
        }

        std::string BuildNodeExpression(
            const std::unordered_map<int, const VisualShaderNode*>& nodesById,
            int nodeId,
            std::unordered_set<int>& visiting)
        {
            const VisualShaderNode* node = FindNode(nodesById, nodeId);
            if (!node)
                return "float4(1.0f, 0.0f, 1.0f, 1.0f)";

            if (!visiting.insert(nodeId).second)
                return "float4(1.0f, 0.0f, 1.0f, 1.0f)";

            auto inputOrDefault = [&](int inputId, const char* fallback)
            {
                if (inputId < 0)
                    return std::string(fallback);
                return BuildNodeExpression(nodesById, inputId, visiting);
            };

            std::string expression;
            switch (node->Type)
            {
                case VisualShaderNodeType::Color:
                    expression = "AlbedoColor";
                    break;
                case VisualShaderNodeType::Texture2D:
                    expression = "AlbedoTexture.Sample(LinearSampler, input.TexCoord)";
                    break;
                case VisualShaderNodeType::Multiply:
                    expression = "(" + inputOrDefault(node->InputA, "float4(1.0f, 1.0f, 1.0f, 1.0f)") +
                        " * " + inputOrDefault(node->InputB, "float4(1.0f, 1.0f, 1.0f, 1.0f)") + ")";
                    break;
                case VisualShaderNodeType::Add:
                    expression = "saturate(" + inputOrDefault(node->InputA, "float4(0.0f, 0.0f, 0.0f, 0.0f)") +
                        " + " + inputOrDefault(node->InputB, "float4(0.0f, 0.0f, 0.0f, 0.0f)") + ")";
                    break;
                case VisualShaderNodeType::Lerp:
                    expression = "lerp(" + inputOrDefault(node->InputA, "float4(0.0f, 0.0f, 0.0f, 1.0f)") +
                        ", " + inputOrDefault(node->InputB, "float4(1.0f, 1.0f, 1.0f, 1.0f)") + ", " +
                        FloatLiteral((std::clamp)(node->Value, 0.0f, 1.0f)) + ")";
                    break;
                case VisualShaderNodeType::Fresnel:
                    expression = "float4(pow(1.0f - saturate(abs(normalize(input.Normal).z)), " +
                        FloatLiteral((std::clamp)(node->Value, 0.1f, 12.0f)) + ").xxx, 1.0f)";
                    break;
                case VisualShaderNodeType::Normal:
                    expression = "float4(normalize(input.Normal) * 0.5f + 0.5f, 1.0f)";
                    break;
                case VisualShaderNodeType::Roughness:
                    expression = "float4(SurfaceValues.xxx, 1.0f)";
                    break;
                case VisualShaderNodeType::Metallic:
                    expression = "float4(SurfaceValues.yyy, 1.0f)";
                    break;
                case VisualShaderNodeType::OneMinus:
                    expression = "saturate(1.0f - " + inputOrDefault(node->InputA, "float4(0.0f, 0.0f, 0.0f, 0.0f)") + ")";
                    break;
                case VisualShaderNodeType::Power:
                    expression = "pow(saturate(" + inputOrDefault(node->InputA, "AlbedoColor") + "), " +
                        FloatLiteral((std::clamp)(node->Value, 0.1f, 12.0f)) + ")";
                    break;
                case VisualShaderNodeType::Saturate:
                    expression = "saturate(" + inputOrDefault(node->InputA, "AlbedoColor") + ")";
                    break;
                case VisualShaderNodeType::UV:
                    expression = "float4(input.TexCoord, 0.0f, 1.0f)";
                    break;
                case VisualShaderNodeType::Time:
                    expression = "float4(SurfaceValues.zzz, 1.0f)";
                    break;
                case VisualShaderNodeType::Output:
                    expression = inputOrDefault(node->InputA, "AlbedoColor");
                    break;
                default:
                    expression = "AlbedoColor";
                    break;
            }

            visiting.erase(nodeId);
            return expression;
        }
    }

    VisualShaderAsset VisualShaderAsset::CreateDefault(const std::string& name)
    {
        VisualShaderAsset asset;
        asset.Name = name.empty() ? "New Visual Shader" : name;

        VisualShaderNode color;
        color.Id = 1;
        color.Type = VisualShaderNodeType::Color;
        color.Name = "Base Color";
        color.Position = { 80.0f, 140.0f };
        color.Color = { 0.8f, 0.8f, 0.85f, 1.0f };

        VisualShaderNode texture;
        texture.Id = 2;
        texture.Type = VisualShaderNodeType::Texture2D;
        texture.Name = "Albedo Texture";
        texture.Position = { 80.0f, 300.0f };

        VisualShaderNode multiply;
        multiply.Id = 3;
        multiply.Type = VisualShaderNodeType::Multiply;
        multiply.Name = "Texture x Color";
        multiply.Position = { 360.0f, 220.0f };
        multiply.InputA = texture.Id;
        multiply.InputB = color.Id;

        VisualShaderNode output;
        output.Id = 4;
        output.Type = VisualShaderNodeType::Output;
        output.Name = "Output";
        output.Position = { 640.0f, 240.0f };
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
                node.Value = nodeData.value("Value", GetDefaultNodeValue(node.Type));
                if (nodeData.contains("Position") && nodeData["Position"].is_array() && nodeData["Position"].size() >= 2)
                {
                    node.Position = {
                        nodeData["Position"][0].get<float>(),
                        nodeData["Position"][1].get<float>()
                    };
                }
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
            nodeData["Position"] = { node.Position.x, node.Position.y };
            nodeData["Color"] = { node.Color.x, node.Color.y, node.Color.z, node.Color.w };
            nodeData["Value"] = node.Value;
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
        int outputNodeId = -1;
        std::unordered_map<int, const VisualShaderNode*> nodesById;
        for (const VisualShaderNode& node : Nodes)
        {
            nodesById[node.Id] = &node;
            if (node.Type == VisualShaderNodeType::Color)
            {
                color = node.Color;
            }
            else if (node.Type == VisualShaderNodeType::Output)
            {
                outputNodeId = node.Id;
            }
        }

        std::unordered_set<int> visiting;
        const std::string finalExpression = outputNodeId >= 0
            ? BuildNodeExpression(nodesById, outputNodeId, visiting)
            : "AlbedoColor";

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
            << "    return " << finalExpression << ";\n"
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
            case VisualShaderNodeType::Add: return "Add";
            case VisualShaderNodeType::Lerp: return "Lerp";
            case VisualShaderNodeType::Fresnel: return "Fresnel";
            case VisualShaderNodeType::Normal: return "Normal";
            case VisualShaderNodeType::Roughness: return "Roughness";
            case VisualShaderNodeType::Metallic: return "Metallic";
            case VisualShaderNodeType::OneMinus: return "OneMinus";
            case VisualShaderNodeType::Power: return "Power";
            case VisualShaderNodeType::Saturate: return "Saturate";
            case VisualShaderNodeType::UV: return "UV";
            case VisualShaderNodeType::Time: return "Time";
            case VisualShaderNodeType::Output: return "Output";
            default: return "Color";
        }
    }

    VisualShaderNodeType VisualShaderAsset::NodeTypeFromString(const std::string& text)
    {
        if (text == "Texture2D") return VisualShaderNodeType::Texture2D;
        if (text == "Multiply") return VisualShaderNodeType::Multiply;
        if (text == "Add") return VisualShaderNodeType::Add;
        if (text == "Lerp") return VisualShaderNodeType::Lerp;
        if (text == "Fresnel") return VisualShaderNodeType::Fresnel;
        if (text == "Normal") return VisualShaderNodeType::Normal;
        if (text == "Roughness") return VisualShaderNodeType::Roughness;
        if (text == "Metallic") return VisualShaderNodeType::Metallic;
        if (text == "OneMinus") return VisualShaderNodeType::OneMinus;
        if (text == "Power") return VisualShaderNodeType::Power;
        if (text == "Saturate") return VisualShaderNodeType::Saturate;
        if (text == "UV") return VisualShaderNodeType::UV;
        if (text == "Time") return VisualShaderNodeType::Time;
        if (text == "Output") return VisualShaderNodeType::Output;
        return VisualShaderNodeType::Color;
    }
}
