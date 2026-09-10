#pragma once

#include "Core.h"
#include "Scene/Components.h"

#include <filesystem>

namespace CCEngine
{
    class CC_API AnimatorControllerAsset
    {
    public:
        static bool SaveToFile(const std::filesystem::path& path, const AnimatorComponent& animator);
        static bool LoadFromFile(const std::filesystem::path& path, AnimatorComponent& animator);
        static void Normalize(AnimatorComponent& animator);

    private:
        static void SyncBaseLayerToLegacyGraph(AnimatorComponent& animator);
        static void ResetRuntimeState(AnimatorComponent& animator);
    };
}
