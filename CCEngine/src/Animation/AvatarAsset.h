#pragma once

#include "Core.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CCEngine
{
    struct ModelComponent;

    struct HumanoidBoneMapping
    {
        std::string HumanBone;
        std::string SourceBone;
        bool Required = false;
    };

    struct HumanoidRetargetPoseOffset
    {
        std::string HumanBone;
        float RotationOffsetX = 0.0f;
        float RotationOffsetY = 0.0f;
        float RotationOffsetZ = 0.0f;
    };

    struct AvatarValidationIssue
    {
        std::string HumanBone;
        std::string SourceBone;
        std::string Message;
        bool Error = false;
    };

    struct AvatarValidationResult
    {
        bool Valid = false;
        uint32_t RequiredMapped = 0;
        uint32_t RequiredMissing = 0;
        uint32_t OptionalMapped = 0;
        std::vector<AvatarValidationIssue> Issues;
    };

    class CC_API AvatarAsset
    {
    public:
        std::string Name = "New Avatar";
        std::string SourceModelGuid;
        std::string SourceModelPath;
        std::string RootBone = "Hips";
        bool OptimizeTransformHierarchy = true;
        std::vector<HumanoidBoneMapping> BoneMappings;
        std::vector<HumanoidRetargetPoseOffset> PoseOffsets;

        static AvatarAsset CreateDefault(const std::string& name);
        static std::vector<HumanoidBoneMapping> CreateDefaultHumanoidMappings();
        static std::vector<HumanoidRetargetPoseOffset> CreateDefaultPoseOffsets();
        static AvatarValidationResult ValidateAgainstModel(const AvatarAsset& avatar, const ModelComponent& model);
        static std::string NormalizeBoneName(const std::string& value);
        static bool ResolveHumanBone(const AvatarAsset& avatar, const std::string& sourceBone, std::string& outHumanBone);
        static bool ResolveSourceBone(const AvatarAsset& avatar, const std::string& humanBone, const ModelComponent* model, std::string& outSourceBone);
        static bool GetPoseOffsetDegrees(const AvatarAsset& avatar, const std::string& humanBone, float& outX, float& outY, float& outZ);
        static bool AutoMapAgainstModel(AvatarAsset& avatar, const ModelComponent& model);

        bool SaveToFile(const std::filesystem::path& path) const;
        static bool LoadFromFile(const std::filesystem::path& path, AvatarAsset& outAvatar);
    };
}
