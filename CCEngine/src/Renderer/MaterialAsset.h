#pragma once

#include "Core.h"
#include "Core/AssetDatabase.h"
#include "Renderer/ShaderProperty.h"
#include "Renderer/Texture.h"
#include "Renderer/SamplerDescription.h"
#include "Renderer/MaterialUVTransform.h"
#include <array>
#include <optional>
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
    enum class MaterialSurfaceMode
    {
        Opaque = 0,
        Transparent = 1
    };

    struct CC_API MaterialAsset
    {
        std::string Name = "New Material";
        std::string ShaderName = "Base3D";
        std::string ShaderGuid;
        std::string ShaderPath;
        DirectX::XMFLOAT4 AlbedoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Roughness = 0.5f;
        float Metallic = 0.0f;
        MaterialSurfaceMode SurfaceMode = MaterialSurfaceMode::Opaque;

        std::string AlbedoTextureGuid;
        std::string AlbedoTexturePath;
        std::string NormalTextureGuid;
        std::string NormalTexturePath;

        std::shared_ptr<Texture2D> AlbedoTexture;
        std::unordered_map<std::string, ShaderPropertyValue> ShaderProperties;
        // 원본 임포트 정보이며 GPU 적용 설정은 아니다. 미지원 항목도 재저장 시 잃지 않는다.
        nlohmann::json ImportedTextureSettings = nlohmann::json::array();
        // 원본 식별과 최초 임포트 기준값. 일반 재질 편집에서는 이 기준을 다시 생성하지 않는다.
        nlohmann::json ImportRecord = nlohmann::json::object();
        std::optional<SamplerDescription> CommonSampler;
        std::array<std::optional<SamplerDescription>, 4> MaterialSamplers{};
        std::array<MaterialUVTransform, 4> UVTransforms{}; // Albedo, Normal, Metallic, Roughness

        static SamplerDescription ReadSampler(const nlohmann::json& value)
        {
            const int u = value.value("U", 1), v = value.value("V", 1);
            const int min = value.value("Min", 1), mag = value.value("Mag", 1);
            const int mip = value.value("Mip", 0), anisotropy = value.value("Anisotropy", 1);
            if (u < 0 || u > 2 || v < 0 || v > 2 || min < 0 || min > 1 || mag < 0 || mag > 1)
                throw std::runtime_error("Invalid material sampler");
            if (mip < 0 || mip > 2 || anisotropy < 1 || anisotropy > 16)
                throw std::runtime_error("Invalid material mip sampler");
            SamplerDescription sampler{ SamplerAddress(u), SamplerAddress(v), SamplerFilter(min), SamplerFilter(mag),
                SamplerMipFilter(mip), static_cast<uint8_t>(anisotropy) };
            sampler.Key();
            return sampler;
        }

        bool LoadFromFile(const std::filesystem::path& path, bool loadTextures = true)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;

            nlohmann::json root;
            file >> root;

            const nlohmann::json& data = root.contains("Material") ? root["Material"] : root;
            Name = data.value("Name", path.stem().string());
            ImportRecord = data.value("ImportRecord", nlohmann::json::object());
            if (!ImportRecord.is_object()) throw std::runtime_error("Invalid material import record");
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
            SurfaceMode = data.value("SurfaceMode", 0) == 1
                ? MaterialSurfaceMode::Transparent
                : MaterialSurfaceMode::Opaque;

            AlbedoTextureGuid = data.value("AlbedoTextureGuid", "");
            AlbedoTexturePath = ResolveAssetPath(AlbedoTextureGuid, data.value("AlbedoTexturePath", ""));
            NormalTextureGuid = data.value("NormalTextureGuid", "");
            NormalTexturePath = ResolveAssetPath(NormalTextureGuid, data.value("NormalTexturePath", ""));

            ShaderProperties.clear();
            CommonSampler.reset();
            MaterialSamplers = {};
            if (data.contains("MaterialSamplers"))
            {
                const auto& samplers = data["MaterialSamplers"];
                if (!samplers.is_array() || samplers.size() != MaterialSamplers.size())
                    throw std::runtime_error("Invalid MaterialSamplers array");
                for (size_t slot = 0; slot < MaterialSamplers.size(); ++slot)
                    if (!samplers[slot].is_null()) MaterialSamplers[slot] = ReadSampler(samplers[slot]);
            }
            UVTransforms = {};
            if (data.contains("UVTransforms"))
            {
                const auto& transforms = data["UVTransforms"];
                if (!transforms.is_array() || transforms.size() != UVTransforms.size())
                    throw std::runtime_error("Invalid material UVTransforms array");
                for (size_t i = 0; i < UVTransforms.size(); ++i)
                {
                    const auto& value = transforms[i];
                    auto& transform = UVTransforms[i];
                    transform.Offset = { value.at("Offset").at(0).get<float>(), value.at("Offset").at(1).get<float>() };
                    transform.Scale = { value.at("Scale").at(0).get<float>(), value.at("Scale").at(1).get<float>() };
                    transform.Rotation = value.at("RotationRadians").get<float>();
                    DirectX::XMFLOAT4 u, v;
                    if (!transform.GetRows(u, v)) throw std::runtime_error("Invalid material UV transform values");
                }
            }
            if (data.contains("CommonSampler") && data["CommonSampler"].is_object())
            {
                const auto& value = data["CommonSampler"];
                // 정수 범위를 먼저 확인한다. 작은 enum으로 변환한 뒤에는 잘못된 값이 잘릴 수 있다.
                CommonSampler = ReadSampler(value);
            }
            ImportedTextureSettings = nlohmann::json::array();
            if (data.contains("ImportedTextureSettings") && data["ImportedTextureSettings"].is_array())
                ImportedTextureSettings = data["ImportedTextureSettings"];
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
            if (loadTextures && !AlbedoTexturePath.empty() && std::filesystem::exists(AlbedoTexturePath))
            {
                // 재질 파일에는 텍스처 포인터를 저장할 수 없다.
                // GUID로 경로를 다시 찾고, 실제 GPU 텍스처는 로드 시점에 다시 만든다.
                AlbedoTexture.reset(Texture2D::Create(AlbedoTexturePath));
            }

            return true;
        }

        nlohmann::json ToJson(bool includeImportRecord = true) const
        {
            nlohmann::json root;
            auto& data = root["Material"];
            if (includeImportRecord && !ImportRecord.empty()) data["ImportRecord"] = ImportRecord;
            data["Name"] = Name;
            data["Shader"] = ShaderName;
            if (!ShaderGuid.empty())
                data["ShaderGuid"] = ShaderGuid;
            if (!ShaderPath.empty())
                data["ShaderPath"] = ShaderPath;
            data["AlbedoColor"] = { AlbedoColor.x, AlbedoColor.y, AlbedoColor.z, AlbedoColor.w };
            data["Roughness"] = Roughness;
            data["Metallic"] = Metallic;
            data["SurfaceMode"] = SurfaceMode == MaterialSurfaceMode::Transparent ? 1 : 0;
            data["UVTransforms"] = nlohmann::json::array();
            data["MaterialSamplers"] = nlohmann::json::array();
            for (const auto& sampler : MaterialSamplers)
            {
                if (!sampler) data["MaterialSamplers"].push_back(nullptr);
                else
                {
                    sampler->Key();
                    data["MaterialSamplers"].push_back({ { "U", int(sampler->U) }, { "V", int(sampler->V) },
                        { "Min", int(sampler->Min) }, { "Mag", int(sampler->Mag) },
                        { "Mip", int(sampler->Mip) }, { "Anisotropy", int(sampler->Anisotropy) } });
                }
            }
            for (const auto& transform : UVTransforms)
            {
                DirectX::XMFLOAT4 u, v;
                if (!transform.GetRows(u, v)) throw std::runtime_error("Invalid material UV transform");
                data["UVTransforms"].push_back({ { "Offset", { transform.Offset.x, transform.Offset.y } },
                    { "Scale", { transform.Scale.x, transform.Scale.y } }, { "RotationRadians", transform.Rotation } });
            }
            if (!ImportedTextureSettings.empty())
                data["ImportedTextureSettings"] = ImportedTextureSettings;
            if (CommonSampler)
            {
                CommonSampler->Key();
                data["CommonSampler"] = { { "U", int(CommonSampler->U) }, { "V", int(CommonSampler->V) },
                    { "Min", int(CommonSampler->Min) }, { "Mag", int(CommonSampler->Mag) },
                    { "Mip", int(CommonSampler->Mip) }, { "Anisotropy", int(CommonSampler->Anisotropy) } };
            }

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

            return root;
        }

        bool SaveToFile(const std::filesystem::path& path) const
        {
            const auto root = ToJson();
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path);
            if (!file.is_open())
                return false;

            file << root.dump(4);
            file.flush();
            return static_cast<bool>(file);
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

        bool IsTransparent() const { return SurfaceMode == MaterialSurfaceMode::Transparent; }

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
