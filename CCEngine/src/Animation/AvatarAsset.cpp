#include "Animation/AvatarAsset.h"
#include "Scene/Components.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>

namespace CCEngine
{
    namespace
    {
        std::string NormalizeBoneKey(std::string value)
        {
            std::string out;
            out.reserve(value.size());
            for (char c : value)
            {
                if (std::isalnum((unsigned char)c))
                    out.push_back((char)std::tolower((unsigned char)c));
            }
            return out;
        }

        std::string LastBoneName(const std::string& value)
        {
            size_t slash = value.find_last_of("/\\");
            return slash == std::string::npos ? value : value.substr(slash + 1);
        }

        std::vector<std::string> BuildSourceBoneNameList(const ModelComponent& model)
        {
            std::vector<std::string> names;
            names.reserve(model.NodeEntityMap.size() + model.NodePathEntityMap.size());

            for (const auto& [name, ignored] : model.NodeEntityMap)
                names.push_back(name);
            for (const auto& [path, ignored] : model.NodePathEntityMap)
                names.push_back(LastBoneName(path));
            if (model.TargetModel)
            {
                std::function<void(const ModelNode&)> collectModelNodes = [&](const ModelNode& node)
                {
                    names.push_back(node.Name);
                    if (!node.Path.empty())
                        names.push_back(LastBoneName(node.Path));
                    for (const auto& child : node.Children)
                        collectModelNodes(child);
                };
                collectModelNodes(model.TargetModel->GetRootNode());
            }

            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        }

        std::vector<std::string> GetBoneAliases(const std::string& humanBone)
        {
            if (humanBone == "Hips") return { "Hips", "mixamorig:Hips" };
            if (humanBone == "Spine") return { "Spine", "mixamorig:Spine" };
            if (humanBone == "Chest") return { "Chest", "Spine1", "mixamorig:Spine1" };
            if (humanBone == "Neck") return { "Neck", "mixamorig:Neck" };
            if (humanBone == "Head") return { "Head", "mixamorig:Head" };
            if (humanBone == "LeftUpperLeg") return { "LeftUpLeg", "Upper_leg.L", "mixamorig:LeftUpLeg" };
            if (humanBone == "LeftLowerLeg") return { "LeftLeg", "Lower_leg.L", "mixamorig:LeftLeg" };
            if (humanBone == "LeftFoot") return { "LeftFoot", "Foot.L", "mixamorig:LeftFoot" };
            if (humanBone == "RightUpperLeg") return { "RightUpLeg", "Upper_leg.R", "mixamorig:RightUpLeg" };
            if (humanBone == "RightLowerLeg") return { "RightLeg", "Lower_leg.R", "mixamorig:RightLeg" };
            if (humanBone == "RightFoot") return { "RightFoot", "Foot.R", "mixamorig:RightFoot" };
            if (humanBone == "LeftUpperArm") return { "LeftArm", "Upper_arm.L", "mixamorig:LeftArm" };
            if (humanBone == "LeftLowerArm") return { "LeftForeArm", "Forearm.L", "mixamorig:LeftForeArm" };
            if (humanBone == "LeftHand") return { "LeftHand", "Hand.L", "mixamorig:LeftHand" };
            if (humanBone == "RightUpperArm") return { "RightArm", "Upper_arm.R", "mixamorig:RightArm" };
            if (humanBone == "RightLowerArm") return { "RightForeArm", "Forearm.R", "mixamorig:RightForeArm" };
            if (humanBone == "RightHand") return { "RightHand", "Hand.R", "mixamorig:RightHand" };
            return { humanBone };
        }

        bool ResolveSourceBoneName(const std::vector<std::string>& modelBoneNames, const HumanoidBoneMapping& mapping, std::string& outName)
        {
            std::vector<std::string> candidates;
            if (!mapping.SourceBone.empty())
                candidates.push_back(mapping.SourceBone);

            const auto aliases = GetBoneAliases(mapping.HumanBone);
            candidates.insert(candidates.end(), aliases.begin(), aliases.end());

            for (const std::string& candidate : candidates)
            {
                const std::string normalizedCandidate = NormalizeBoneKey(candidate);
                for (const std::string& modelBoneName : modelBoneNames)
                {
                    if (NormalizeBoneKey(modelBoneName) == normalizedCandidate)
                    {
                        outName = modelBoneName;
                        return true;
                    }
                }
            }

            return false;
        }
    }

    AvatarAsset AvatarAsset::CreateDefault(const std::string& name)
    {
        AvatarAsset avatar;
        avatar.Name = name.empty() ? "New Avatar" : name;
        avatar.BoneMappings = CreateDefaultHumanoidMappings();
        avatar.PoseOffsets = CreateDefaultPoseOffsets();
        return avatar;
    }

    std::vector<HumanoidBoneMapping> AvatarAsset::CreateDefaultHumanoidMappings()
    {
        // HumanBone은 엔진이 이해하는 표준 이름이고, SourceBone은 FBX 안 실제 본 이름이다.
        // SourceBone을 비워 두면 검증 단계에서 Mixamo/Maya/Blender에서 자주 쓰는 이름을 자동으로 찾아본다.
        return {
            { "Hips", "", true },
            { "Spine", "", true },
            { "Chest", "", false },
            { "Neck", "", false },
            { "Head", "", true },
            { "LeftUpperLeg", "", true },
            { "LeftLowerLeg", "", true },
            { "LeftFoot", "", true },
            { "RightUpperLeg", "", true },
            { "RightLowerLeg", "", true },
            { "RightFoot", "", true },
            { "LeftUpperArm", "", true },
            { "LeftLowerArm", "", true },
            { "LeftHand", "", true },
            { "RightUpperArm", "", true },
            { "RightLowerArm", "", true },
            { "RightHand", "", true }
        };
    }

    std::vector<HumanoidRetargetPoseOffset> AvatarAsset::CreateDefaultPoseOffsets()
    {
        std::vector<HumanoidRetargetPoseOffset> offsets;
        for (const HumanoidBoneMapping& mapping : CreateDefaultHumanoidMappings())
            offsets.push_back({ mapping.HumanBone, 0.0f, 0.0f, 0.0f });
        return offsets;
    }

    AvatarValidationResult AvatarAsset::ValidateAgainstModel(const AvatarAsset& avatar, const ModelComponent& model)
    {
        AvatarValidationResult result;
        const std::vector<std::string> modelBoneNames = BuildSourceBoneNameList(model);
        const std::string normalizedRoot = NormalizeBoneKey(avatar.RootBone);

        bool rootFound = false;
        for (const std::string& boneName : modelBoneNames)
        {
            if (NormalizeBoneKey(boneName) == normalizedRoot)
            {
                rootFound = true;
                break;
            }
        }

        if (!rootFound)
        {
            result.Issues.push_back({ "Root", avatar.RootBone, "Root bone not found in model.", true });
            ++result.RequiredMissing;
        }

        for (const HumanoidBoneMapping& mapping : avatar.BoneMappings)
        {
            std::string resolvedBone;
            if (ResolveSourceBoneName(modelBoneNames, mapping, resolvedBone))
            {
                if (mapping.Required)
                    ++result.RequiredMapped;
                else
                    ++result.OptionalMapped;
                continue;
            }

            result.Issues.push_back({
                mapping.HumanBone,
                mapping.SourceBone.empty() ? "(auto)" : mapping.SourceBone,
                mapping.Required ? "Required humanoid bone not mapped." : "Optional humanoid bone not mapped.",
                mapping.Required
            });

            if (mapping.Required)
                ++result.RequiredMissing;
        }

        result.Valid = result.RequiredMissing == 0;
        return result;
    }

    std::string AvatarAsset::NormalizeBoneName(const std::string& value)
    {
        return NormalizeBoneKey(value);
    }

    bool AvatarAsset::ResolveHumanBone(const AvatarAsset& avatar, const std::string& sourceBone, std::string& outHumanBone)
    {
        const std::string sourceKey = NormalizeBoneKey(sourceBone);
        if (sourceKey.empty())
            return false;

        for (const HumanoidBoneMapping& mapping : avatar.BoneMappings)
        {
            std::vector<std::string> candidates;
            if (!mapping.SourceBone.empty())
                candidates.push_back(mapping.SourceBone);
            const auto aliases = GetBoneAliases(mapping.HumanBone);
            candidates.insert(candidates.end(), aliases.begin(), aliases.end());

            for (const std::string& candidate : candidates)
            {
                if (NormalizeBoneKey(candidate) == sourceKey)
                {
                    outHumanBone = mapping.HumanBone;
                    return true;
                }
            }
        }

        return false;
    }

    bool AvatarAsset::ResolveSourceBone(const AvatarAsset& avatar, const std::string& humanBone, const ModelComponent* model, std::string& outSourceBone)
    {
        auto mappingIt = std::find_if(avatar.BoneMappings.begin(), avatar.BoneMappings.end(), [&humanBone](const HumanoidBoneMapping& mapping)
        {
            return mapping.HumanBone == humanBone;
        });
        if (mappingIt == avatar.BoneMappings.end())
            return false;

        if (model)
        {
            const std::vector<std::string> modelBoneNames = BuildSourceBoneNameList(*model);
            return ResolveSourceBoneName(modelBoneNames, *mappingIt, outSourceBone);
        }

        if (!mappingIt->SourceBone.empty())
        {
            outSourceBone = mappingIt->SourceBone;
            return true;
        }

        const auto aliases = GetBoneAliases(mappingIt->HumanBone);
        if (!aliases.empty())
        {
            outSourceBone = aliases.front();
            return true;
        }

        return false;
    }

    bool AvatarAsset::GetPoseOffsetDegrees(const AvatarAsset& avatar, const std::string& humanBone, float& outX, float& outY, float& outZ)
    {
        auto offsetIt = std::find_if(avatar.PoseOffsets.begin(), avatar.PoseOffsets.end(), [&humanBone](const HumanoidRetargetPoseOffset& offset)
        {
            return offset.HumanBone == humanBone;
        });
        if (offsetIt == avatar.PoseOffsets.end())
            return false;

        outX = offsetIt->RotationOffsetX;
        outY = offsetIt->RotationOffsetY;
        outZ = offsetIt->RotationOffsetZ;
        return std::fabs(outX) > 0.0001f || std::fabs(outY) > 0.0001f || std::fabs(outZ) > 0.0001f;
    }

    bool AvatarAsset::AutoMapAgainstModel(AvatarAsset& avatar, const ModelComponent& model)
    {
        bool changed = false;
        const std::vector<std::string> modelBoneNames = BuildSourceBoneNameList(model);
        for (HumanoidBoneMapping& mapping : avatar.BoneMappings)
        {
            std::string resolvedBone;
            if (!ResolveSourceBoneName(modelBoneNames, mapping, resolvedBone))
                continue;

            if (mapping.SourceBone != resolvedBone)
            {
                // 자동 매핑은 검증에서 찾은 실제 본 이름을 파일에 굳혀 둔다.
                // 다음 로드부터는 별칭 검색 없이 바로 같은 본을 찾을 수 있다.
                mapping.SourceBone = resolvedBone;
                changed = true;
            }
        }
        return changed;
    }

    bool AvatarAsset::SaveToFile(const std::filesystem::path& path) const
    {
        nlohmann::json data;
        data["Version"] = 1;
        data["Name"] = Name;
        data["SourceModelGuid"] = SourceModelGuid;
        data["SourceModelPath"] = SourceModelPath;
        data["RootBone"] = RootBone;
        data["OptimizeTransformHierarchy"] = OptimizeTransformHierarchy;

        data["BoneMappings"] = nlohmann::json::array();
        for (const HumanoidBoneMapping& mapping : BoneMappings)
        {
            data["BoneMappings"].push_back({
                { "HumanBone", mapping.HumanBone },
                { "SourceBone", mapping.SourceBone },
                { "Required", mapping.Required }
            });
        }

        data["PoseOffsets"] = nlohmann::json::array();
        for (const HumanoidRetargetPoseOffset& offset : PoseOffsets)
        {
            data["PoseOffsets"].push_back({
                { "HumanBone", offset.HumanBone },
                { "RotationOffsetX", offset.RotationOffsetX },
                { "RotationOffsetY", offset.RotationOffsetY },
                { "RotationOffsetZ", offset.RotationOffsetZ }
            });
        }

        std::ofstream stream(path);
        if (!stream.is_open())
            return false;

        stream << data.dump(4);
        return true;
    }

    bool AvatarAsset::LoadFromFile(const std::filesystem::path& path, AvatarAsset& outAvatar)
    {
        std::ifstream stream(path);
        if (!stream.is_open())
            return false;

        nlohmann::json data;
        try
        {
            stream >> data;
        }
        catch (...)
        {
            return false;
        }

        outAvatar = AvatarAsset{};
        outAvatar.Name = data.value("Name", path.stem().string());
        outAvatar.SourceModelGuid = data.value("SourceModelGuid", "");
        outAvatar.SourceModelPath = data.value("SourceModelPath", "");
        outAvatar.RootBone = data.value("RootBone", "Hips");
        outAvatar.OptimizeTransformHierarchy = data.value("OptimizeTransformHierarchy", true);
        outAvatar.BoneMappings.clear();
        outAvatar.PoseOffsets.clear();

        if (data.contains("BoneMappings") && data["BoneMappings"].is_array())
        {
            for (const auto& item : data["BoneMappings"])
            {
                if (!item.is_object())
                    continue;

                outAvatar.BoneMappings.push_back({
                    item.value("HumanBone", ""),
                    item.value("SourceBone", ""),
                    item.value("Required", false)
                });
            }
        }

        if (outAvatar.BoneMappings.empty())
            outAvatar.BoneMappings = CreateDefaultHumanoidMappings();

        if (data.contains("PoseOffsets") && data["PoseOffsets"].is_array())
        {
            for (const auto& item : data["PoseOffsets"])
            {
                if (!item.is_object())
                    continue;

                outAvatar.PoseOffsets.push_back({
                    item.value("HumanBone", ""),
                    item.value("RotationOffsetX", 0.0f),
                    item.value("RotationOffsetY", 0.0f),
                    item.value("RotationOffsetZ", 0.0f)
                });
            }
        }

        for (const HumanoidBoneMapping& mapping : outAvatar.BoneMappings)
        {
            const bool exists = std::any_of(outAvatar.PoseOffsets.begin(), outAvatar.PoseOffsets.end(), [&mapping](const HumanoidRetargetPoseOffset& offset)
            {
                return offset.HumanBone == mapping.HumanBone;
            });
            if (!exists)
                outAvatar.PoseOffsets.push_back({ mapping.HumanBone, 0.0f, 0.0f, 0.0f });
        }

        return true;
    }
}
