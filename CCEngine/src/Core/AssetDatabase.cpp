#include "Core/AssetDatabase.h"

#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_map>

namespace CCEngine
{
    namespace
    {
        std::unordered_map<std::string, AssetMetadata> s_GuidToMetadata;
        std::unordered_map<std::string, std::string> s_PathToGuid;

        std::filesystem::path NormalizePath(const std::filesystem::path& path)
        {
            std::error_code ec;
            auto normalized = std::filesystem::weakly_canonical(path, ec);
            if (!ec)
                return normalized;

            auto absolute = std::filesystem::absolute(path, ec);
            if (!ec)
                return absolute;

            return path;
        }

        std::string NormalizeKey(const std::filesystem::path& path)
        {
            return NormalizePath(path).generic_string();
        }

        std::string GenerateGuid()
        {
            static std::random_device rd;
            static std::mt19937_64 rng(rd());
            static std::uniform_int_distribution<uint64_t> dist;

            std::stringstream ss;
            ss << std::hex << std::setfill('0')
                << std::setw(16) << dist(rng)
                << std::setw(16) << dist(rng);
            return ss.str();
        }

        std::filesystem::path MakePortablePath(const std::filesystem::path& path)
        {
            std::error_code ec;
            auto relative = std::filesystem::relative(path, std::filesystem::current_path(), ec);
            if (!ec && !relative.empty())
                return relative;

            return path;
        }

        std::string GetImporterForKind(AssetKind kind)
        {
            switch (kind)
            {
                case AssetKind::Scene: return "SceneImporter";
                case AssetKind::Prefab: return "PrefabImporter";
                case AssetKind::Model: return "ModelImporter";
                case AssetKind::Texture: return "TextureImporter";
                default: return "UnknownImporter";
            }
        }

        std::filesystem::path GetAssetPathFromMetaPath(const std::filesystem::path& metaPath)
        {
            std::string text = metaPath.string();
            const std::string suffix = ".meta";
            if (text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix)
                text.resize(text.size() - suffix.size());

            return text;
        }

        bool ReadMetaFile(const std::filesystem::path& metaPath, AssetMetadata& metadata)
        {
            std::ifstream in(metaPath);
            if (!in.is_open())
                return false;

            nlohmann::json data;
            try
            {
                in >> data;
            }
            catch (...)
            {
                return false;
            }

            metadata.Guid = data.value("guid", "");
            metadata.Type = AssetDatabase::AssetKindFromString(data.value("type", "Unknown"));
            metadata.SourcePath = data.value("sourcePath", GetAssetPathFromMetaPath(metaPath).generic_string());
            metadata.Importer = data.value("importer", GetImporterForKind(metadata.Type));
            metadata.Version = data.value("version", 1u);
            return !metadata.Guid.empty();
        }

        bool WriteMetaFile(const AssetMetadata& metadata)
        {
            if (metadata.SourcePath.empty())
                return false;

            nlohmann::json data;
            data["guid"] = metadata.Guid;
            data["type"] = AssetDatabase::AssetKindToString(metadata.Type);
            data["sourcePath"] = MakePortablePath(metadata.SourcePath).generic_string();
            data["importer"] = metadata.Importer;
            data["version"] = metadata.Version;

            std::error_code ec;
            auto metaPath = AssetDatabase::GetMetaPath(metadata.SourcePath);
            auto parentPath = metaPath.parent_path();
            if (!parentPath.empty() && !std::filesystem::exists(parentPath, ec))
                return false;

            std::ofstream out(metaPath);
            if (!out.is_open())
                return false;

            out << data.dump(4);
            return true;
        }

        void RegisterMetadata(const AssetMetadata& metadata)
        {
            if (metadata.Guid.empty() || metadata.SourcePath.empty())
                return;

            s_GuidToMetadata[metadata.Guid] = metadata;
            s_PathToGuid[NormalizeKey(metadata.SourcePath)] = metadata.Guid;
        }
    }

    void AssetDatabase::Scan(const std::filesystem::path& rootDirectory)
    {
        s_GuidToMetadata.clear();
        s_PathToGuid.clear();

        std::error_code ec;
        if (!std::filesystem::exists(rootDirectory, ec) || ec)
            return;

        // 에디터가 프로젝트 에셋을 볼 때마다 sidecar meta를 맞춘다.
        // 그래서 저장 파일은 경로 대신 GUID를 우선 믿을 수 있다.
        std::filesystem::recursive_directory_iterator it(
            rootDirectory,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        std::filesystem::recursive_directory_iterator end;

        while (!ec && it != end)
        {
            const auto entryPath = it->path();

            if (!it->is_regular_file(ec) || ec)
            {
                ec.clear();
                it.increment(ec);
                continue;
            }

            if (entryPath.extension() == ".meta")
            {
                it.increment(ec);
                continue;
            }

            if (GetAssetKind(entryPath) == AssetKind::Unknown)
            {
                it.increment(ec);
                continue;
            }

            EnsureMetaFile(entryPath);
            it.increment(ec);
        }
    }

    std::string AssetDatabase::GetGuidFromPath(const std::filesystem::path& assetPath)
    {
        if (GetAssetKind(assetPath) == AssetKind::Unknown)
            return "";

        EnsureMetaFile(assetPath);

        auto it = s_PathToGuid.find(NormalizeKey(assetPath));
        if (it != s_PathToGuid.end())
            return it->second;

        AssetMetadata metadata;
        if (ReadMetaFile(GetMetaPath(assetPath), metadata))
        {
            metadata.SourcePath = assetPath;
            RegisterMetadata(metadata);
            return metadata.Guid;
        }

        return "";
    }

    std::filesystem::path AssetDatabase::GetPathFromGuid(const std::string& guid)
    {
        if (guid.empty())
            return {};

        auto it = s_GuidToMetadata.find(guid);
        if (it == s_GuidToMetadata.end())
        {
            // 씬 로드가 에셋 브라우저보다 먼저 일어날 수 있어 기본 assets 폴더를 한 번 훑는다.
            Scan("assets");
            it = s_GuidToMetadata.find(guid);
        }

        if (it == s_GuidToMetadata.end())
            return {};

        return it->second.SourcePath;
    }

    AssetKind AssetDatabase::GetAssetKind(const std::filesystem::path& assetPath)
    {
        std::string extension = assetPath.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        if (extension == ".ccscene") return AssetKind::Scene;
        if (extension == ".ccprefab") return AssetKind::Prefab;
        if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb") return AssetKind::Model;
        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga") return AssetKind::Texture;
        return AssetKind::Unknown;
    }

    std::filesystem::path AssetDatabase::GetMetaPath(const std::filesystem::path& assetPath)
    {
        return assetPath.string() + ".meta";
    }

    bool AssetDatabase::EnsureMetaFile(const std::filesystem::path& assetPath)
    {
        AssetKind kind = GetAssetKind(assetPath);
        std::error_code ec;
        if (kind == AssetKind::Unknown || !std::filesystem::exists(assetPath, ec) || ec)
            return false;

        AssetMetadata metadata;
        const auto metaPath = GetMetaPath(assetPath);
        if (std::filesystem::exists(metaPath, ec) && !ec)
        {
            if (!ReadMetaFile(metaPath, metadata))
                metadata.Guid = GenerateGuid();
        }
        else
        {
            metadata.Guid = GenerateGuid();
        }

        // 파일을 옮겨도 meta의 GUID는 유지하고, 현재 위치만 갱신한다.
        metadata.Type = kind;
        metadata.SourcePath = assetPath;
        metadata.Importer = GetImporterForKind(kind);
        metadata.Version = 1;

        if (!WriteMetaFile(metadata))
            return false;

        RegisterMetadata(metadata);
        return true;
    }

    bool AssetDatabase::DeleteMetaFile(const std::filesystem::path& assetPath)
    {
        std::error_code ec;
        auto metaPath = GetMetaPath(assetPath);
        if (std::filesystem::exists(metaPath, ec) && !ec)
            std::filesystem::remove(metaPath, ec);

        s_PathToGuid.erase(NormalizeKey(assetPath));
        return !ec;
    }

    std::string AssetDatabase::AssetKindToString(AssetKind kind)
    {
        switch (kind)
        {
            case AssetKind::Scene: return "Scene";
            case AssetKind::Prefab: return "Prefab";
            case AssetKind::Model: return "Model";
            case AssetKind::Texture: return "Texture";
            default: return "Unknown";
        }
    }

    AssetKind AssetDatabase::AssetKindFromString(const std::string& text)
    {
        if (text == "Scene") return AssetKind::Scene;
        if (text == "Prefab") return AssetKind::Prefab;
        if (text == "Model") return AssetKind::Model;
        if (text == "Texture") return AssetKind::Texture;
        return AssetKind::Unknown;
    }
}
