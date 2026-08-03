#pragma once

#include "Core.h"
#include "Core/AssetDatabase.h"
#include "Renderer/Texture.h"
#include "json.hpp"

#include <DirectXMath.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace CCEngine
{
    struct CC_API MaterialAsset
    {
        std::string Name = "New Material";
        std::string ShaderName = "Base3D";
        DirectX::XMFLOAT4 AlbedoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Roughness = 0.5f;
        float Metallic = 0.0f;

        std::string AlbedoTextureGuid;
        std::string AlbedoTexturePath;
        std::string NormalTextureGuid;
        std::string NormalTexturePath;

        std::shared_ptr<Texture2D> AlbedoTexture;

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
