#pragma once

#include "Core.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace CCEngine
{
    struct CC_API ShaderAsset
    {
        std::string Name = "New Shader";
        std::string TemplateName = "Lit";
        std::string Language = "HLSL";
        std::string VertexEntry = "VSMain";
        std::string PixelEntry = "PSMain";
        std::string Source;

        bool LoadFromFile(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;

            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (extension == ".hlsl")
            {
                std::stringstream buffer;
                buffer << file.rdbuf();
                Name = path.stem().string();
                TemplateName = "HLSL";
                Language = "HLSL";
                Source = buffer.str();
                return true;
            }

            nlohmann::json root;
            file >> root;

            const nlohmann::json& data = root.contains("Shader") ? root["Shader"] : root;
            Name = data.value("Name", path.stem().string());
            TemplateName = data.value("Template", "Lit");
            Language = data.value("Language", "HLSL");
            VertexEntry = data.value("VertexEntry", "VSMain");
            PixelEntry = data.value("PixelEntry", "PSMain");
            Source = data.value("Source", MakeTemplateSource(Name, TemplateName));
            return true;
        }

        bool SaveToFile(const std::filesystem::path& path) const
        {
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());

            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (extension == ".hlsl")
            {
                std::ofstream file(path);
                if (!file.is_open())
                    return false;

                // 현재 기본 셰이더 에셋은 순수 HLSL 파일이다.
                // GUID와 템플릿 정보는 meta/머티리얼 참조가 맡고, 실제 코드는 Visual Studio에서 그대로 편집한다.
                file << (Source.empty() ? MakeTemplateSource(Name, TemplateName) : Source);
                return true;
            }

            nlohmann::json root;
            auto& data = root["Shader"];
            data["Name"] = Name;
            data["Template"] = TemplateName;
            data["Language"] = Language;
            data["VertexEntry"] = VertexEntry;
            data["PixelEntry"] = PixelEntry;
            data["Source"] = Source.empty() ? MakeTemplateSource(Name, TemplateName) : Source;

            std::ofstream file(path);
            if (!file.is_open())
                return false;

            file << root.dump(4);
            return true;
        }

        static ShaderAsset CreateTemplate(const std::string& name, const std::string& templateName = "Lit")
        {
            ShaderAsset shader;
            shader.Name = name.empty() ? "New Shader" : name;
            shader.TemplateName = templateName.empty() ? "Lit" : templateName;
            shader.Source = MakeTemplateSource(shader.Name, shader.TemplateName);
            return shader;
        }

        static std::string MakeTemplateSource(const std::string& shaderName, const std::string& templateName)
        {
            std::string displayName = shaderName.empty() ? "New Shader" : shaderName;
            std::string mode = templateName.empty() ? "Lit" : templateName;

            // 기본 템플릿은 실제 HLSL 파일로 저장된다.
            // 사용자는 Visual Studio에서 이 코드를 직접 고치고, 엔진은 GUID와 컴파일 파이프라인만 관리한다.
            return
                "// " + displayName + " - " + mode + " shader template\n"
                "// @property Color AlbedoColor = 1,1,1,1\n"
                "// @property Texture2D AlbedoTexture\n"
                "// @property Float Roughness = 0.5 range(0,1)\n"
                "// @property Float Metallic = 0.0 range(0,1)\n"
                "// @property Toggle UseDetail = false\n"
                "cbuffer CameraBuffer : register(b0)\n"
                "{\n"
                "    matrix ViewProjection;\n"
                "};\n\n"
                "cbuffer ObjectBuffer : register(b1)\n"
                "{\n"
                "    matrix Transform;\n"
                "};\n\n"
                "cbuffer MaterialPropertyBuffer : register(b4)\n"
                "{\n"
                "    float4 AlbedoColor;\n"
                "    float4 PropertyColors[8];\n"
                "    float4 PropertyScalars[4];\n"
                "    float4 PropertyToggles[4];\n"
                "    float4 SurfaceValues; // x: Roughness, y: Metallic\n"
                "};\n\n"
                "Texture2D AlbedoTexture : register(t0);\n"
                "SamplerState LinearSampler : register(s0);\n\n"
                "struct VSInput\n"
                "{\n"
                "    float3 Position : POSITION;\n"
                "    float3 Normal : NORMAL;\n"
                "    float2 TexCoord : TEXCOORD0;\n"
                "};\n\n"
                "struct PSInput\n"
                "{\n"
                "    float4 Position : SV_POSITION;\n"
                "    float3 Normal : NORMAL;\n"
                "    float2 TexCoord : TEXCOORD0;\n"
                "};\n\n"
                "PSInput VSMain(VSInput input)\n"
                "{\n"
                "    PSInput output;\n"
                "    float4 worldPosition = mul(float4(input.Position, 1.0f), Transform);\n"
                "    output.Position = mul(worldPosition, ViewProjection);\n"
                "    output.Normal = input.Normal;\n"
                "    output.TexCoord = input.TexCoord;\n"
                "    return output;\n"
                "}\n\n"
                "float4 PSMain(PSInput input) : SV_TARGET\n"
                "{\n"
                "    float4 textureColor = AlbedoTexture.Sample(LinearSampler, input.TexCoord);\n"
                "    return textureColor * AlbedoColor;\n"
                "}\n";
        }
    };
}
