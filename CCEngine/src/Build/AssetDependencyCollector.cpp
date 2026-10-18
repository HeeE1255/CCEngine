#include "Build/AssetDependencyCollector.h"

#include "json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <fstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace CCEngine
{
    namespace
    {
        struct ReferenceFieldPair
        {
            const char* GuidKey;
            const char* PathKey;
        };

        constexpr std::array<ReferenceFieldPair, 13> ReferencePairs = {{
            { "AlbedoGuid", "AlbedoPath" },
            { "MaterialGuid", "MaterialPath" },
            { "Guid", "Path" },
            { "ControllerGuid", "ControllerPath" },
            { "SourceGuid", "SourcePath" },
            { "AvatarGuid", "AvatarPath" },
            { "SourceAvatarGuid", "SourceAvatarPath" },
            { "ShaderGuid", "ShaderPath" },
            { "AlbedoTextureGuid", "AlbedoTexturePath" },
            { "NormalTextureGuid", "NormalTexturePath" },
            { "TextureGuid", "TexturePath" },
            { "MotionGuid", "MotionPath" },
            { "SourceModelGuid", "SourceModelPath" }
        }};

        std::string PathKey(const std::filesystem::path& path)
        {
            std::string value = path.lexically_normal().generic_string();
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        bool IsInside(const std::filesystem::path& child, const std::filesystem::path& parent)
        {
            const auto relative = child.lexically_normal().lexically_relative(parent.lexically_normal());
            if (relative.empty() || relative.is_absolute())
                return false;
            for (const auto& part : relative)
            {
                if (part == "..")
                    return false;
            }
            return true;
        }

        std::string JsonString(const nlohmann::json& object, const char* key)
        {
            if (!object.contains(key) || !object[key].is_string())
                return {};
            return object[key].get<std::string>();
        }

        bool ReadJson(const std::filesystem::path& path, nlohmann::json& json)
        {
            std::ifstream input(path);
            if (!input.is_open())
                return false;
            try
            {
                input >> json;
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool IsReferenceContainer(AssetKind kind)
        {
            return kind == AssetKind::Scene ||
                kind == AssetKind::Prefab ||
                kind == AssetKind::Material ||
                kind == AssetKind::VisualShader ||
                kind == AssetKind::AnimatorController ||
                kind == AssetKind::Avatar;
        }

        std::filesystem::path AssetPathFromMeta(const std::filesystem::path& metaPath)
        {
            std::string path = metaPath.string();
            constexpr const char* suffix = ".meta";
            if (path.size() >= 5 && path.compare(path.size() - 5, 5, suffix) == 0)
                path.resize(path.size() - 5);
            return path;
        }

        std::filesystem::path ResolveStoredPath(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& sourceFile,
            const std::string& storedPath)
        {
            if (storedPath.empty())
                return {};

            std::filesystem::path path = storedPath;
            if (path.is_absolute())
                return path.lexically_normal();

            // 현재 저장 포맷은 대부분 프로젝트 기준 assets/...를 쓴다. 오래된 Material처럼
            // 에셋 파일 기준 상대 경로인 경우도 있으므로 프로젝트 기준을 먼저 확인한다.
            std::error_code ec;
            const auto projectRelative = (projectRoot / path).lexically_normal();
            if (std::filesystem::exists(projectRelative, ec) && !ec)
                return projectRelative;

            ec.clear();
            return (sourceFile.parent_path() / path).lexically_normal();
        }

        void CollectJsonReferences(
            const nlohmann::json& value,
            const std::string& location,
            std::vector<std::tuple<std::string, std::string, std::string>>& references)
        {
            if (value.is_object())
            {
                for (const ReferenceFieldPair& pair : ReferencePairs)
                {
                    const std::string guid = JsonString(value, pair.GuidKey);
                    const std::string path = JsonString(value, pair.PathKey);
                    if (!guid.empty() || !path.empty())
                        references.emplace_back(guid, path, location + "/" + pair.PathKey);
                }

                // Audio는 과거부터 path-only 저장을 허용했지만 다른 에셋과 같은 재귀 수집 대상으로 취급한다.
                const std::string audioGuid = JsonString(value, "AudioGuid");
                const std::string audioPath = JsonString(value, "AudioPath");
                if (!audioGuid.empty() || !audioPath.empty())
                    references.emplace_back(audioGuid, audioPath, location + "/AudioPath");

                for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
                    CollectJsonReferences(iterator.value(), location + "/" + iterator.key(), references);
            }
            else if (value.is_array())
            {
                for (size_t index = 0; index < value.size(); ++index)
                    CollectJsonReferences(value[index], location + "[" + std::to_string(index) + "]", references);
            }
        }

        using ScriptAssetFieldMap = std::unordered_map<std::string, std::unordered_set<std::string>>;

        ScriptAssetFieldMap LoadScriptAssetFields(const std::filesystem::path& projectRoot)
        {
            ScriptAssetFieldMap fields;
            nlohmann::json manifest;
            if (!ReadJson(projectRoot / "assets" / "Scripts" / "Build" / "GameScripts.manifest.json", manifest))
                return fields;

            for (const auto& classData : manifest.value("Classes", nlohmann::json::array()))
            {
                const std::string className = classData.value("ClassName", "");
                if (className.empty())
                    continue;
                for (const auto& fieldData : classData.value("Fields", nlohmann::json::array()))
                {
                    const auto editor = fieldData.value("Editor", nlohmann::json::object());
                    if (editor.value("AssetReference", false))
                        fields[className].insert(fieldData.value("Name", ""));
                }
            }
            return fields;
        }

        void CollectScriptAssetReferences(
            const nlohmann::json& value,
            const std::string& location,
            const ScriptAssetFieldMap& assetFields,
            std::vector<std::tuple<std::string, std::string, std::string>>& references)
        {
            if (value.is_object())
            {
                if (value.contains("ScriptComponent") && value["ScriptComponent"].is_object())
                {
                    const auto& script = value["ScriptComponent"];
                    const std::string className = script.value("ClassName", "");
                    const auto classFields = assetFields.find(className);
                    if (classFields != assetFields.end() && script.contains("Fields") && script["Fields"].is_object())
                    {
                        for (const std::string& fieldName : classFields->second)
                        {
                            const auto field = script["Fields"].find(fieldName);
                            if (field == script["Fields"].end() || !field->is_string() || field->get<std::string>().empty())
                                continue;
                            const std::string stored = field->get<std::string>();
                            references.emplace_back("", stored, location + "/ScriptComponent/Fields/" + fieldName);
                        }
                    }
                }
                for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
                    CollectScriptAssetReferences(iterator.value(), location + "/" + iterator.key(), assetFields, references);
            }
            else if (value.is_array())
            {
                for (size_t index = 0; index < value.size(); ++index)
                    CollectScriptAssetReferences(value[index], location + "[" + std::to_string(index) + "]", assetFields, references);
            }
        }

        void AddModelCompanionFiles(
            const AssetDependency& model,
            const std::filesystem::path& assetsRoot,
            std::deque<AssetDependency>& pending)
        {
            std::error_code ec;
            for (std::filesystem::directory_iterator iterator(model.SourcePath.parent_path(), ec), end;
                 !ec && iterator != end;
                 iterator.increment(ec))
            {
                if (!iterator->is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }

                const AssetKind kind = AssetDatabase::GetAssetKind(iterator->path());
                std::string extension = iterator->path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });

                if (kind != AssetKind::Texture && extension != ".bin" && extension != ".mtl")
                    continue;

                AssetDependency companion;
                companion.SourcePath = iterator->path().lexically_normal();
                companion.RelativePath = companion.SourcePath.lexically_relative(assetsRoot);
                companion.Kind = kind;
                companion.ReferencedBy = model.SourcePath;
                companion.JsonLocation = "/ModelCompanion";
                if (!companion.RelativePath.empty() && !companion.RelativePath.is_absolute())
                    pending.push_back(std::move(companion));
                ec.clear();
            }
        }
    }

    AssetDependencyReport AssetDependencyCollector::Collect(
        const std::filesystem::path& projectRootInput,
        const std::filesystem::path& startSceneInput,
        const std::vector<std::filesystem::path>& alwaysIncludeAssets) const
    {
        AssetDependencyReport report;
        std::error_code ec;
        const auto projectRoot = std::filesystem::absolute(projectRootInput, ec).lexically_normal();
        const auto assetsRoot = (projectRoot / "assets").lexically_normal();
        const auto startScene = std::filesystem::absolute(startSceneInput, ec).lexically_normal();
        if (ec || !IsInside(startScene, assetsRoot))
        {
            report.Issues.push_back({ startSceneInput, {}, {}, {}, "Start scene is outside the project assets directory." });
            return report;
        }

        const ScriptAssetFieldMap scriptAssetFields = LoadScriptAssetFields(projectRoot);
        std::unordered_map<std::string, std::filesystem::path> guidIndex;
        std::unordered_map<std::string, std::string> pathGuidIndex;
        for (std::filesystem::recursive_directory_iterator iterator(
                assetsRoot,
                std::filesystem::directory_options::skip_permission_denied,
                ec), end;
             !ec && iterator != end;
             iterator.increment(ec))
        {
            if (!iterator->is_regular_file(ec) || ec || iterator->path().extension() != ".meta")
            {
                ec.clear();
                continue;
            }

            nlohmann::json metadata;
            if (!ReadJson(iterator->path(), metadata))
                continue;
            const std::string guid = metadata.value("guid", "");
            if (guid.empty())
                continue;

            const auto sourcePath = AssetPathFromMeta(iterator->path()).lexically_normal();
            auto [found, inserted] = guidIndex.emplace(guid, sourcePath);
            if (!inserted && PathKey(found->second) != PathKey(sourcePath))
            {
                report.Issues.push_back({ iterator->path(), "/guid", guid, sourcePath.string(), "Duplicate asset GUID in read-only build index." });
            }
            else
            {
                pathGuidIndex[PathKey(sourcePath)] = guid;
            }
        }
        if (ec)
        {
            report.Issues.push_back({ assetsRoot, {}, {}, {}, "Failed while building the read-only GUID index: " + ec.message() });
            return report;
        }

        std::deque<AssetDependency> pending;
        AssetDependency root;
        root.SourcePath = startScene;
        root.RelativePath = startScene.lexically_relative(assetsRoot);
        root.Kind = AssetKind::Scene;
        root.JsonLocation = "/StartScene";
        pending.push_back(std::move(root));

        for (const auto& alwaysInclude : alwaysIncludeAssets)
        {
            AssetDependency dependency;
            dependency.SourcePath = alwaysInclude.is_absolute() ? alwaysInclude : projectRoot / alwaysInclude;
            dependency.SourcePath = dependency.SourcePath.lexically_normal();
            dependency.RelativePath = dependency.SourcePath.lexically_relative(assetsRoot);
            dependency.Kind = AssetDatabase::GetAssetKind(dependency.SourcePath);
            dependency.JsonLocation = "/AlwaysInclude";
            if (!dependency.RelativePath.empty() && !dependency.RelativePath.is_absolute())
                pending.push_back(std::move(dependency));
        }

        std::unordered_set<std::string> visited;
        while (!pending.empty())
        {
            AssetDependency dependency = std::move(pending.front());
            pending.pop_front();

            dependency.SourcePath = dependency.SourcePath.lexically_normal();
            const std::string key = PathKey(dependency.SourcePath);
            if (!visited.insert(key).second)
                continue;

            if (!IsInside(dependency.SourcePath, assetsRoot))
            {
                report.Issues.push_back({ dependency.ReferencedBy, dependency.JsonLocation, dependency.Guid, dependency.SourcePath.string(), "Referenced asset is outside the project assets directory." });
                continue;
            }

            if (!std::filesystem::is_regular_file(dependency.SourcePath, ec) || ec)
            {
                report.Issues.push_back({ dependency.ReferencedBy, dependency.JsonLocation, dependency.Guid, dependency.SourcePath.string(), "Referenced asset file is missing." });
                ec.clear();
                continue;
            }

            dependency.RelativePath = dependency.SourcePath.lexically_relative(assetsRoot);
            if (dependency.RelativePath.empty() || dependency.RelativePath.is_absolute())
            {
                report.Issues.push_back({ dependency.ReferencedBy, dependency.JsonLocation, dependency.Guid, dependency.SourcePath.string(), "Failed to make project-relative asset path." });
                continue;
            }
            if (dependency.Kind == AssetKind::Unknown)
                dependency.Kind = AssetDatabase::GetAssetKind(dependency.SourcePath);
            if (dependency.Guid.empty())
            {
                const auto guid = pathGuidIndex.find(key);
                if (guid != pathGuidIndex.end())
                    dependency.Guid = guid->second;
            }
            report.Assets.push_back(dependency);

            if (dependency.Kind == AssetKind::Model)
                AddModelCompanionFiles(dependency, assetsRoot, pending);

            if (dependency.Kind == AssetKind::VisualShader)
            {
                auto generated = dependency.SourcePath;
                generated.replace_extension(".generated.hlsl");
                if (std::filesystem::is_regular_file(generated, ec) && !ec)
                {
                    AssetDependency generatedDependency;
                    generatedDependency.SourcePath = generated;
                    generatedDependency.RelativePath = generated.lexically_relative(assetsRoot);
                    generatedDependency.Kind = AssetKind::Shader;
                    generatedDependency.ReferencedBy = dependency.SourcePath;
                    generatedDependency.JsonLocation = "/GeneratedShader";
                    if (!generatedDependency.RelativePath.empty() && !generatedDependency.RelativePath.is_absolute())
                        pending.push_back(std::move(generatedDependency));
                }
                ec.clear();
            }

            if (!IsReferenceContainer(dependency.Kind))
                continue;

            nlohmann::json json;
            if (!ReadJson(dependency.SourcePath, json))
            {
                report.Issues.push_back({ dependency.SourcePath, {}, dependency.Guid, dependency.SourcePath.string(), "Referenced JSON asset could not be parsed." });
                continue;
            }

            std::vector<std::tuple<std::string, std::string, std::string>> references;
            CollectJsonReferences(json, "", references);
            if (dependency.Kind == AssetKind::Scene || dependency.Kind == AssetKind::Prefab)
                CollectScriptAssetReferences(json, "", scriptAssetFields, references);
            for (const auto& [guid, storedPath, location] : references)
            {
                std::filesystem::path resolved;
                if (!guid.empty())
                {
                    auto found = guidIndex.find(guid);
                    if (found != guidIndex.end())
                        resolved = found->second;
                    else
                    {
                        report.Issues.push_back({ dependency.SourcePath, location, guid, storedPath, "Referenced GUID is missing from the build index." });
                        continue;
                    }
                }
                else
                {
                    resolved = ResolveStoredPath(projectRoot, dependency.SourcePath, storedPath);
                }

                AssetDependency child;
                child.SourcePath = resolved;
                child.Guid = guid;
                child.Kind = AssetDatabase::GetAssetKind(resolved);
                child.ReferencedBy = dependency.SourcePath;
                child.JsonLocation = location;
                pending.push_back(std::move(child));
            }
        }

        std::sort(report.Assets.begin(), report.Assets.end(), [](const AssetDependency& left, const AssetDependency& right)
        {
            return left.RelativePath.generic_string() < right.RelativePath.generic_string();
        });
        report.Success = report.Issues.empty();
        return report;
    }
}
