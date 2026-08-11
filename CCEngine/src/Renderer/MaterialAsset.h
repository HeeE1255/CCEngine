#pragma once

#include "Core.h"
#include "Core/AssetDatabase.h"
#include "Renderer/ShaderProperty.h"
#include "Renderer/Texture.h"
#include "json.hpp"

#include <DirectXMath.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

namespace CCEngine
{
    struct CC_API MaterialAsset
    {
        std::string Name = "New Material";
        std::string ShaderName = "Base3D";
        std::string ShaderGuid;
        std::string ShaderPath;
        DirectX::XMFLOAT4 AlbedoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Roughness = 0.5f;
        float Metallic = 0.0f;

        std::string AlbedoTextureGuid;
        std::string AlbedoTexturePath;
        std::string NormalTextureGuid;
        std::string NormalTexturePath;

        std::shared_ptr<Texture2D> AlbedoTexture;
        std::unordered_map<std::string, ShaderPropertyValue> ShaderProperties;

        bool LoadFromFile(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;

            nlohmann::json root;
            file >> root;

            const nlohmann::json& data = root.contains("Material") ? root["Material"] : root;
            Name = data.value("Name", path.stem().string());
            ShaderName = data.value("Shader", "Base3D");
            ShaderGuid = data.value("ShaderGuid", "");
            ShaderPath = ResolveAssetPath(ShaderGuid, data.value("ShaderPath", ""));
            if (!ShaderPath.empty())
                ShaderName = std::filesystem::path(ShaderPath).stem().string();
            if (data.contains("AlbedoColor") && data["AlbedoColor"].is_array() && data["AlbedoColor"].size() >= 4)
            {
                AlbedoColor = {
                    data["AlbedoColor"][0].get<float>(),
                    data["AlbedoColor"][1].get<float>(),
                    data["AlbedoColor"][2].get<float>(),
                    data["AlbedoColor"][3].get<float>()
                };
            }
            Roughness = std::clamp(data.value("Roughness", Roughness), 0.0f, 1.0f);
            Metallic = std::clamp(data.value("Metallic", Metallic), 0.0f, 1.0f);

            AlbedoTextureGuid = data.value("AlbedoTextureGuid", "");
            AlbedoTexturePath = ResolveAssetPath(AlbedoTextureGuid, data.value("AlbedoTexturePath", ""));
            NormalTextureGuid = data.value("NormalTextureGuid", "");
            NormalTexturePath = ResolveAssetPath(NormalTextureGuid, data.value("NormalTexturePath", ""));

            ShaderProperties.clear();
            if (data.contains("ShaderProperties") && data["ShaderProperties"].is_object())
            {
                for (auto it = data["ShaderProperties"].begin(); it != data["ShaderProperties"].end(); ++it)
                {
                    const nlohmann::json& propertyData = it.value();
                    ShaderPropertyValue value;
                    value.Type = ShaderPropertyParser::TypeFromString(propertyData.value("Type", "Unknown"));
                    if (propertyData.contains("Color") && propertyData["Color"].is_array() && propertyData["Color"].size() >= 4)
                    {
                        value.Color = {
                            propertyData["Color"][0].get<float>(),
                            propertyData["Color"][1].get<float>(),
                            propertyData["Color"][2].get<float>(),
                            propertyData["Color"][3].get<float>()
                        };
                    }
                    value.FloatValue = propertyData.value("Float", value.FloatValue);
                    value.BoolValue = propertyData.value("Bool", value.BoolValue);
                    value.TextureGuid = propertyData.value("TextureGuid", "");
                    value.TexturePath = ResolveAssetPath(value.TextureGuid, propertyData.value("TexturePath", ""));
                    ShaderProperties[it.key()] = value;
                }
            }

            AlbedoTexture.reset();
            if (!AlbedoTexturePath.empty() && std::filesystem::exists(AlbedoTexturePath))
            {
                // 재질 파일에는 텍스처 포인터를 저장할 수 없다.
                // GUID로 경로를 다시 찾고, 실제 GPU 텍스처는 로드 시점에 다시 만든다.
                AlbedoTexture.reset(Texture2D::Create(AlbedoTexturePath));
            }

            return true;
        }

        bool SaveToFile(const std::filesystem::path& path) const
        {
            std::filesystem::create_directories(path.parent_path());

            nlohmann::json root;
            auto& data = root["Material"];
            data["Name"] = Name;
            data["Shader"] = ShaderName;
            if (!ShaderGuid.empty())
                data["ShaderGuid"] = ShaderGuid;
            if (!ShaderPath.empty())
                data["ShaderPath"] = ShaderPath;
            data["AlbedoColor"] = { AlbedoColor.x, AlbedoColor.y, AlbedoColor.z, AlbedoColor.w };
            data["Roughness"] = Roughness;
            data["Metallic"] = Metallic;

            // 참조는 GUID를 우선 저장하고, 경로는 구버전/복구용 보조 정보로 남긴다.
            // 파일명을 바꿔도 meta GUID가 유지되면 같은 텍스처를 다시 찾을 수 있다.
            if (!AlbedoTextureGuid.empty())
                data["AlbedoTextureGuid"] = AlbedoTextureGuid;
            if (!AlbedoTexturePath.empty())
                data["AlbedoTexturePath"] = AlbedoTexturePath;
            if (!NormalTextureGuid.empty())
                data["NormalTextureGuid"] = NormalTextureGuid;
            if (!NormalTexturePath.empty())
                data["NormalTexturePath"] = NormalTexturePath;

            if (!ShaderProperties.empty())
            {
                auto& propertyRoot = data["ShaderProperties"];
                for (const auto& [name, value] : ShaderProperties)
                {
                    auto& propertyData = propertyRoot[name];
                    propertyData["Type"] = ShaderPropertyParser::TypeToString(value.Type);
                    if (value.Type == ShaderPropertyType::Color)
                        propertyData["Color"] = { value.Color.x, value.Color.y, value.Color.z, value.Color.w };
                    else if (value.Type == ShaderPropertyType::Float)
                        propertyData["Float"] = value.FloatValue;
                    else if (value.Type == ShaderPropertyType::Toggle)
                        propertyData["Bool"] = value.BoolValue;
                    else if (value.Type == ShaderPropertyType::Texture2D)
                    {
                        if (!value.TextureGuid.empty())
                            propertyData["TextureGuid"] = value.TextureGuid;
                        if (!value.TexturePath.empty())
                            propertyData["TexturePath"] = value.TexturePath;
                    }
                }
            }

            std::ofstream file(path);
            if (!file.is_open())
                return false;

            file << root.dump(4);
            return true;
        }

        static MaterialAsset CreateDefault(const std::string& name)
        {
            MaterialAsset material;
            material.Name = name.empty() ? "New Material" : name;
            return material;
        }

        ShaderPropertyValue& EnsureShaderPropertyValue(const ShaderPropertyDefinition& definition)
        {
            ShaderPropertyValue& value = ShaderProperties[definition.Name];
            if (value.Type == ShaderPropertyType::Unknown)
            {
                value.Type = definition.Type;
                value.Color = definition.DefaultColor;
                value.FloatValue = definition.DefaultFloat;
                value.BoolValue = definition.DefaultBool;
                value.TexturePath = definition.DefaultTexturePath;
                if (!value.TexturePath.empty())
                    value.TextureGuid = AssetDatabase::GetGuidFromPath(value.TexturePath);
            }
            return value;
        }

    private:
        static std::string ResolveAssetPath(const std::string& guid, const std::string& storedPath)
        {
            if (!guid.empty())
            {
                std::filesystem::path resolved = AssetDatabase::GetPathFromGuid(guid);
                if (!resolved.empty())
                    return resolved.string();
            }

            return storedPath;
        }
    };
}
