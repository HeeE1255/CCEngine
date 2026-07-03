#include "Core/AssetDatabase.h"

#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef CC_PLATFORM_WINDOWS
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace CCEngine
{
    namespace
    {
        std::unordered_map<std::string, AssetMetadata> s_GuidToMetadata;
        std::unordered_map<std::string, std::string> s_PathToGuid;
        std::filesystem::path s_LastScanRoot = "assets";
        bool s_HasScanned = false;
        bool s_IsDirty = true;

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

        std::string CalculateFileHash(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
                return "";

            // 빠른 식별용 해시다. 암호화 목적이 아니라, 이동된 파일과 고아 meta를 다시 짝짓는 데 쓴다.
            uint64_t hash = 14695981039346656037ull;
            char buffer[4096];
            while (file)
            {
                file.read(buffer, sizeof(buffer));
                std::streamsize count = file.gcount();
                for (std::streamsize i = 0; i < count; ++i)
                {
                    hash ^= static_cast<unsigned char>(buffer[i]);
                    hash *= 1099511628211ull;
                }
            }

            std::stringstream ss;
            ss << std::hex << std::setfill('0') << std::setw(16) << hash;
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
            metadata.FileHash = data.value("fileHash", "");
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
            data["fileHash"] = metadata.FileHash;
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

        bool IsGuidUsedByAnotherAsset(const std::string& guid, const std::filesystem::path& assetPath)
        {
            auto it = s_GuidToMetadata.find(guid);
            if (it == s_GuidToMetadata.end())
                return false;

            return NormalizeKey(it->second.SourcePath) != NormalizeKey(assetPath);
        }

        bool TryAdoptOrphanMeta(const std::filesystem::path& assetPath, std::vector<AssetMetadata>& orphanMetas)
        {
            AssetKind kind = AssetDatabase::GetAssetKind(assetPath);
            std::string fileHash = CalculateFileHash(assetPath);
            if (fileHash.empty())
                return false;

            for (auto it = orphanMetas.begin(); it != orphanMetas.end(); ++it)
            {
                if (it->Type != kind || it->FileHash.empty() || it->FileHash != fileHash)
                    continue;

                std::error_code ec;
                std::filesystem::path oldMetaPath = AssetDatabase::GetMetaPath(it->SourcePath);
                std::filesystem::path newMetaPath = AssetDatabase::GetMetaPath(assetPath);

                // 파일만 이동되고 meta가 예전 위치에 남았을 때, 해시가 같으면 같은 에셋으로 본다.
                // 이때 meta를 새 위치로 옮겨 GUID를 끊지 않는다.
                if (std::filesystem::exists(oldMetaPath, ec) && !ec)
                    std::filesystem::rename(oldMetaPath, newMetaPath, ec);
                if (ec)
                    return false;

                orphanMetas.erase(it);
                return true;
            }

            return false;
        }

        void UnregisterMetadataForPath(const std::filesystem::path& assetPath)
        {
            auto key = NormalizeKey(assetPath);
            auto guidIt = s_PathToGuid.find(key);
            if (guidIt != s_PathToGuid.end())
            {
                s_GuidToMetadata.erase(guidIt->second);
                s_PathToGuid.erase(guidIt);
            }
        }

        void UnregisterMetadataUnderDirectory(const std::filesystem::path& directory)
        {
            std::string directoryKey = NormalizeKey(directory);
            if (!directoryKey.empty() && directoryKey.back() != '/')
                directoryKey += '/';

            for (auto it = s_PathToGuid.begin(); it != s_PathToGuid.end();)
            {
                if (it->first.rfind(directoryKey, 0) == 0)
                {
                    s_GuidToMetadata.erase(it->second);
                    it = s_PathToGuid.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        bool MovePathToRecycleBin(const std::filesystem::path& path)
        {
#ifdef CC_PLATFORM_WINDOWS
            std::wstring from = std::filesystem::absolute(path).wstring();
            from.push_back(L'\0');
            from.push_back(L'\0');

            SHFILEOPSTRUCTW operation = {};
            operation.wFunc = FO_DELETE;
            operation.pFrom = from.c_str();
            operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;

            return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
#else
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            return !ec;
#endif
        }
    }

    void AssetDatabase::Scan(const std::filesystem::path& rootDirectory)
    {
        s_GuidToMetadata.clear();
        s_PathToGuid.clear();
        s_LastScanRoot = rootDirectory;
        s_HasScanned = false;

        std::error_code ec;
        if (!std::filesystem::exists(rootDirectory, ec) || ec)
            return;

        std::vector<AssetMetadata> orphanMetas;

        // 먼저 고아 meta를 모은다. asset은 없고 meta만 남은 경우, 뒤에서 같은 해시의 새 파일과 다시 연결한다.
        std::filesystem::recursive_directory_iterator metaIt(
            rootDirectory,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        std::filesystem::recursive_directory_iterator end;

        while (!ec && metaIt != end)
        {
            const auto entryPath = metaIt->path();
            if (metaIt->is_regular_file(ec) && !ec && entryPath.extension() == ".meta")
            {
                AssetMetadata metadata;
                if (ReadMetaFile(entryPath, metadata))
                {
                    std::filesystem::path source = metadata.SourcePath;
                    if (!std::filesystem::exists(source, ec) || ec)
                    {
                        ec.clear();
                        orphanMetas.push_back(metadata);
                    }
                }
            }
            ec.clear();
            metaIt.increment(ec);
        }

        ec.clear();

        // 에디터가 프로젝트 에셋을 볼 때마다 sidecar meta를 맞춘다.
        // 그래서 저장 파일은 경로 대신 GUID를 우선 믿을 수 있다.
        std::filesystem::recursive_directory_iterator it(
            rootDirectory,
            std::filesystem::directory_options::skip_permission_denied,
            ec);

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

            if (!std::filesystem::exists(GetMetaPath(entryPath), ec) || ec)
            {
                ec.clear();
                TryAdoptOrphanMeta(entryPath, orphanMetas);
            }

            EnsureMetaFile(entryPath);
            it.increment(ec);
        }

        // 전체 스캔을 마친 뒤에는 캐시가 현재 디스크 상태를 대표한다.
        // 이후 UI는 이 캐시를 읽고, 파일 작업이 생길 때만 다시 더럽다고 표시한다.
        s_HasScanned = true;
        s_IsDirty = false;
    }

    void AssetDatabase::ScanIfNeeded(const std::filesystem::path& rootDirectory)
    {
        std::string requestedRoot = NormalizeKey(rootDirectory);
        std::string currentRoot = NormalizeKey(s_LastScanRoot);

        if (!s_HasScanned || s_IsDirty || requestedRoot != currentRoot)
        {
            Scan(rootDirectory);
        }
    }

    void AssetDatabase::MarkDirty(const std::filesystem::path& rootDirectory)
    {
        if (!rootDirectory.empty())
            s_LastScanRoot = rootDirectory;

        // 파일 감시자가 없는 현재 구조에서는 에셋 생성/삭제/이동 코드가 직접 호출한다.
        // 더럽다고만 표시하면 다음 조회 시점에 한 번만 전체 스캔한다.
        s_IsDirty = true;
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
            // 씬 로드가 에셋 브라우저보다 먼저 일어날 수 있어 마지막 에셋 루트를 한 번 훑는다.
            // 프로젝트마다 에셋 루트가 다를 수 있으므로 하드코딩한 경로보다 마지막 스캔 위치를 우선한다.
            ScanIfNeeded(s_LastScanRoot.empty() ? std::filesystem::path("assets") : s_LastScanRoot);
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

        // 복사된 에셋은 meta까지 같이 복사되어 GUID가 겹칠 수 있다.
        // 같은 GUID가 이미 다른 파일에 등록되어 있으면 새 에셋으로 보고 GUID를 다시 만든다.
        if (IsGuidUsedByAnotherAsset(metadata.Guid, assetPath))
            metadata.Guid = GenerateGuid();

        // 파일을 옮겨도 meta의 GUID는 유지하고, 현재 위치만 갱신한다.
        metadata.Type = kind;
        metadata.SourcePath = assetPath;
        metadata.Importer = GetImporterForKind(kind);
        metadata.FileHash = CalculateFileHash(assetPath);
        metadata.Version = 1;

        if (!WriteMetaFile(metadata))
            return false;

        RegisterMetadata(metadata);
        return true;
    }

    bool AssetDatabase::MoveAsset(const std::filesystem::path& from, const std::filesystem::path& to)
    {
        std::error_code ec;
        if (GetAssetKind(from) == AssetKind::Unknown || GetAssetKind(to) == AssetKind::Unknown)
            return false;
        if (!std::filesystem::exists(from, ec) || ec || std::filesystem::exists(to, ec))
            return false;

        auto toParent = to.parent_path();
        if (!toParent.empty())
            std::filesystem::create_directories(toParent, ec);
        if (ec)
            return false;

        auto fromMeta = GetMetaPath(from);
        auto toMeta = GetMetaPath(to);

        // 에셋 이동은 파일과 meta를 한 묶음으로 처리한다.
        // 그래야 씬과 프리팹이 들고 있는 GUID가 이름 변경 뒤에도 그대로 이어진다.
        std::filesystem::rename(from, to, ec);
        if (ec)
            return false;

        if (std::filesystem::exists(fromMeta, ec) && !ec)
        {
            std::filesystem::rename(fromMeta, toMeta, ec);
            if (ec)
            {
                std::error_code rollbackEc;
                std::filesystem::rename(to, from, rollbackEc);
                return false;
            }
        }

        s_PathToGuid.erase(NormalizeKey(from));
        EnsureMetaFile(to);
        MarkDirty(s_LastScanRoot);
        return true;
    }

    bool AssetDatabase::RenameAsset(const std::filesystem::path& assetPath, const std::string& newName)
    {
        if (newName.empty())
            return false;

        std::filesystem::path target = assetPath.parent_path() / newName;
        if (target.extension().empty())
            target.replace_extension(assetPath.extension());

        return MoveAsset(assetPath, target);
    }

    bool AssetDatabase::DeleteAsset(const std::filesystem::path& assetPath)
    {
        std::error_code ec;
        if (GetAssetKind(assetPath) == AssetKind::Unknown || !std::filesystem::exists(assetPath, ec) || ec)
            return false;

        std::filesystem::remove(assetPath, ec);
        if (ec)
            return false;

        return DeleteMetaFile(assetPath);
    }

    bool AssetDatabase::RecycleAsset(const std::filesystem::path& assetPath)
    {
        std::error_code ec;
        if (!std::filesystem::exists(assetPath, ec) || ec)
            return false;

        bool isDirectory = std::filesystem::is_directory(assetPath, ec) && !ec;
        bool isRegularFile = std::filesystem::is_regular_file(assetPath, ec) && !ec;
        if (!isDirectory && !isRegularFile)
            return false;

        if (isRegularFile && GetAssetKind(assetPath) == AssetKind::Unknown)
            return false;

        auto metaPath = GetMetaPath(assetPath);

        // 삭제는 즉시 제거하지 않고 OS 휴지통으로 보낸다.
        // 파일과 sidecar meta를 함께 처리해야 복구했을 때 GUID가 그대로 살아난다.
        if (!MovePathToRecycleBin(assetPath))
            return false;

        if (std::filesystem::exists(metaPath, ec) && !ec)
            MovePathToRecycleBin(metaPath);

        if (isDirectory)
            UnregisterMetadataUnderDirectory(assetPath);
        else
            UnregisterMetadataForPath(assetPath);

        MarkDirty(s_LastScanRoot);
        return true;
    }

    bool AssetDatabase::DeleteMetaFile(const std::filesystem::path& assetPath)
    {
        std::error_code ec;
        auto key = NormalizeKey(assetPath);
        auto guidIt = s_PathToGuid.find(key);
        if (guidIt != s_PathToGuid.end())
            s_GuidToMetadata.erase(guidIt->second);

        auto metaPath = GetMetaPath(assetPath);
        if (std::filesystem::exists(metaPath, ec) && !ec)
            std::filesystem::remove(metaPath, ec);

        s_PathToGuid.erase(key);
        MarkDirty(s_LastScanRoot);
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
