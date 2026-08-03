#pragma once
#include "Core.h"
#include "UI/WindowPanel.h"
#include "UI/Widget.h"
#include "Scene/Entity.h"
#include "Renderer/MaterialAsset.h"
#include <filesystem>
#include <vector>
#include <functional>

namespace CCEngine::UI { class Button; class ImageWidget; class Panel; class TextInput; }
namespace CCEngine { class Framebuffer; class Mesh; }

namespace CCEngine 
{
    namespace UI 
    {

        class CC_API InspectorPanel : public WindowPanel
        {
        public:
            InspectorPanel(const std::string& name, const std::string& title);
            ~InspectorPanel() override;

            // 외부(하이어라키 등)에서 선택된 엔티티를 세팅
            void SetSelectedEntity(Entity entity);
            void SetSelectedAsset(const std::filesystem::path& assetPath, const std::string& assetType);
            Entity GetSelectedEntity() const { return m_SelectedEntity; }
            bool ClearSelectedAssetIfMissing();
            void RequestRebuild() { m_NeedsRebuild = true; }
            void SetAssetChangedCallback(std::function<void(const std::filesystem::path&, const std::string&)> callback)
            {
                m_OnAssetChanged = std::move(callback);
            }
            void SetMaterialPreviewChangedCallback(std::function<void(const std::filesystem::path&, const MaterialAsset&)> callback)
            {
                m_OnMaterialPreviewChanged = std::move(callback);
            }
            void SetMaterialPreviewCapturedCallback(std::function<void(const std::filesystem::path&, uint32_t, uint32_t, const std::vector<uint32_t>&)> callback)
            {
                m_OnMaterialPreviewCaptured = std::move(callback);
            }
            void SetMaterialPreviewTextureReadyCallback(std::function<void(const std::filesystem::path&, RendererHandle)> callback)
            {
                m_OnMaterialPreviewTextureReady = std::move(callback);
            }
            void SetSceneStructureChangeCallbacks(
                std::function<void(const std::string&)> beginChange,
                std::function<void()> commitChange)
            {
                m_BeginStructureChange = std::move(beginChange);
                m_CommitStructureChange = std::move(commitChange);
            }
            void BeginStructureChange(const std::string& label);
            void CommitStructureChange();
            bool IsAlbedoTextureSlotPoint(float mouseX, float mouseY) const;
            bool IsMaterialSlotPoint(float mouseX, float mouseY) const;

            virtual void OnRender() override;
            virtual void OnUpdate(float deltaTime) override;
            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;
            virtual bool OnEvent(Event& e) override;
            virtual bool WantsMouseCapture() const override { return WindowPanel::WantsMouseCapture() || m_IsDraggingScrollbar; }

        private:
            enum class AddComponentType
            {
                Mesh,
                Light,
                Camera,
                SpriteRenderer,
                Rigidbody2D,
                BoxCollider2D,
                BoxCollider3D,
                SphereCollider3D,
                CylinderCollider3D,
                MeshCollider3D,
                Script
            };
            void RebuildInspector();
            void BuildMaterialInspector();
            void MarkSelectedMaterialDirty();
            void FlushSelectedMaterialSave();
            void SaveSelectedMaterial();
            void EnsureMaterialPreviewResources();
            void RenderSelectedMaterialPreview();
            bool IsMaterialPreviewPoint(float mouseX, float mouseY) const;
            void BuildAddComponentMenu();
            void AddComponent(AddComponentType type);
            bool CreateAndAttachScript();
            void AttachExistingScript(const std::string& className);
            std::vector<std::string> DiscoverScriptClasses() const;
            void FilterAddComponentMenu(const std::string& query);
            void ChangeComponentPage(int direction);

            Entity m_SelectedEntity;
            std::filesystem::path m_SelectedAssetPath;
            std::string m_SelectedAssetType;
            MaterialAsset m_SelectedMaterial;
            Framebuffer* m_MaterialPreviewFramebuffer = nullptr;
            std::shared_ptr<Mesh> m_MaterialPreviewMesh;
            UI::ImageWidget* m_MaterialPreviewImage = nullptr;
            bool m_MaterialPreviewDirty = true;
            bool m_IsDraggingMaterialPreview = false;
            float m_MaterialPreviewYaw = 0.45f;
            float m_MaterialPreviewPitch = -0.20f;
            float m_MaterialPreviewDragStartX = 0.0f;
            float m_MaterialPreviewDragStartY = 0.0f;
            float m_MaterialPreviewDragStartYaw = 0.0f;
            float m_MaterialPreviewDragStartPitch = 0.0f;
            ScrollState m_ScrollState;
            bool m_IsDraggingScrollbar = false;
            float m_DragMouseStartY = 0.0f;
            float m_DragScrollStartY = 0.0f;
            Button* m_AddComponentButton = nullptr;
            Panel* m_AddComponentMenu = nullptr;
            TextInput* m_ComponentSearchInput = nullptr;
            std::vector<std::pair<Button*, std::string>> m_ComponentButtons;
            Button* m_PreviousComponentPage = nullptr;
            Button* m_ComponentPageLabel = nullptr;
            Button* m_NextComponentPage = nullptr;
            std::string m_ComponentFilter;
            size_t m_ComponentPage = 0;
            static constexpr size_t ComponentPageSize = 10;
            std::function<void(const std::string&)> m_BeginStructureChange;
            std::function<void()> m_CommitStructureChange;
            std::function<void(const std::filesystem::path&, const std::string&)> m_OnAssetChanged;
            std::function<void(const std::filesystem::path&, const MaterialAsset&)> m_OnMaterialPreviewChanged;
            std::function<void(const std::filesystem::path&, uint32_t, uint32_t, const std::vector<uint32_t>&)> m_OnMaterialPreviewCaptured;
            std::function<void(const std::filesystem::path&, RendererHandle)> m_OnMaterialPreviewTextureReady;
            bool m_NeedsRebuild = false;
            bool m_MaterialSavePending = false;
            float m_MaterialSaveCountdown = 0.0f;
            static constexpr float MaterialSaveDelaySeconds = 0.35f;
        };

    }
}
