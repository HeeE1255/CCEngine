#pragma once

#include "Core/Layer.h"
#include "Runtime/RuntimeManifest.h"

#include <filesystem>

namespace CCEngine
{
    class Scene;

    class CC_API GameLayer final : public Layer
    {
    public:
        GameLayer(RuntimeManifestData manifest, std::filesystem::path runtimeRoot);
        ~GameLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnRender() override;

        bool IsLoaded() const { return m_Loaded; }

    private:
        void FailStartup(const std::string& message);

        RuntimeManifestData m_Manifest;
        std::filesystem::path m_RuntimeRoot;
        std::filesystem::path m_ContentRoot;
        Scene* m_Scene = nullptr;
        bool m_Loaded = false;
        bool m_ReportedMissingCamera = false;
    };
}
