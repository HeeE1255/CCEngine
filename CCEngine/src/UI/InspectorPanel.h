#pragma once
#include "Core.h"
#include "UI/WindowPanel.h"
#include "UI/Widget.h"
#include "Scene/Entity.h"
#include "Renderer/MaterialAsset.h"
#include <chrono>
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
            enum class ColliderEditShape { Box, Sphere, Cylinder };
            enum class ColliderSnapValue { Offset, Size, Radius, Height };

            InspectorPanel(const std::string& name, const std::string& title);
            ~InspectorPanel() override;
            static void ShutdownSharedCaches();

            // 외부(하이어라키 등)에서 선택된 엔티티를 세팅
            void SetSelectedEntity(Entity entity);
            void SetSelectedAnimatorState(Entity entity, int layerIndex, int stateIndex);
            void SetSelectedAnimatorTransition(Entity entity, int layerIndex, int transitionIndex);
            void SetSelectedAsset(const std::filesystem::path& assetPath, const std::string& assetType);
            Entity GetSelectedEntity() const { return m_SelectedEntity; }
            bool HasSelectedAsset() const { return !m_SelectedAssetPath.empty(); }
            bool IsInspectingAnimatorState() const { return m_HasSelectedAnimatorState || m_HasSelectedAnimatorTransition; }
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
            void SetShaderEditorOpenCallback(std::function<void(const std::filesystem::path&)> callback)
            {
                m_OnOpenShaderEditor = std::move(callback);
            }
            void SetAnimatorClipPickRequestedCallback(std::function<void(Entity, int, int)> callback)
            {
                m_OnAnimatorClipPickRequested = std::move(callback);
            }
            void SetColliderEditCallbacks(
                std::function<bool(Entity, ColliderEditShape)> isActive,
                std::function<void(Entity, ColliderEditShape)> toggle)
            {
                m_IsColliderEditActive = std::move(isActive);
                m_OnToggleColliderEdit = std::move(toggle);
            }
            void SetColliderSnapCallbacks(
                std::function<bool()> isEnabled,
                std::function<void(bool)> setEnabled,
                std::function<float(ColliderSnapValue)> getStep,
                std::function<void(ColliderSnapValue, float)> setStep)
            {
                m_IsColliderSnapEnabled = std::move(isEnabled);
                m_SetColliderSnapEnabled = std::move(setEnabled);
                m_GetColliderSnapStep = std::move(getStep);
                m_SetColliderSnapStep = std::move(setStep);
            }
            bool IsColliderEditActive(Entity entity, ColliderEditShape shape) const
            {
                return m_IsColliderEditActive && m_IsColliderEditActive(entity, shape);
            }
            void ToggleColliderEdit(Entity entity, ColliderEditShape shape)
            {
                if (m_OnToggleColliderEdit)
                    m_OnToggleColliderEdit(entity, shape);
            }
            bool IsColliderSnapEnabled() const
            {
                return m_IsColliderSnapEnabled && m_IsColliderSnapEnabled();
            }
            void SetColliderSnapEnabled(bool enabled)
            {
                if (m_SetColliderSnapEnabled)
                    m_SetColliderSnapEnabled(enabled);
            }
            float GetColliderSnapStep(ColliderSnapValue value) const
            {
                return m_GetColliderSnapStep ? m_GetColliderSnapStep(value) : 0.1f;
            }
            void SetColliderSnapStep(ColliderSnapValue value, float step)
            {
                if (m_SetColliderSnapStep)
                    m_SetColliderSnapStep(value, step);
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
                Rigidbody3D,
                BoxCollider2D,
                BoxCollider3D,
                SphereCollider3D,
                CylinderCollider3D,
                MeshCollider3D,
                Audio,
                Animator,
                Script
            };
            void RebuildInspector();
            void BuildMaterialInspector();
            void BuildShaderInspector();
            void BuildAvatarInspector();
            void BuildAnimatorStateInspector();
            void BuildAnimatorTransitionInspector();
            bool ClearSelectedAnimatorStateClip();
            void BuildGenericAssetInspector();
            MaterialAsset BuildShaderPreviewMaterial(const std::filesystem::path& shaderPath) const;
            void MarkSelectedMaterialDirty();
            void FlushSelectedMaterialSave();
            void SaveSelectedMaterial();
            bool UndoMaterialEdit();
            bool RedoMaterialEdit();
            void ResetMaterialUndoBaseline();
            bool SameMaterialForUndo(const MaterialAsset& a, const MaterialAsset& b) const;
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
            bool m_HasSelectedAnimatorState = false;
            bool m_HasSelectedAnimatorTransition = false;
            bool m_AnimatorStateClipSlotSelected = false;
            bool m_ShowAnimatorAvailableClips = false;
            std::chrono::steady_clock::time_point m_LastAnimatorClipSlotClickTime{};
            int m_SelectedAnimatorLayerIndex = -1;
            int m_SelectedAnimatorStateIndex = -1;
            int m_SelectedAnimatorTransitionIndex = -1;
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
            std::function<void(const std::filesystem::path&)> m_OnOpenShaderEditor;
            std::function<void(Entity, int, int)> m_OnAnimatorClipPickRequested;
            std::function<bool(Entity, ColliderEditShape)> m_IsColliderEditActive;
            std::function<void(Entity, ColliderEditShape)> m_OnToggleColliderEdit;
            std::function<bool()> m_IsColliderSnapEnabled;
            std::function<void(bool)> m_SetColliderSnapEnabled;
            std::function<float(ColliderSnapValue)> m_GetColliderSnapStep;
            std::function<void(ColliderSnapValue, float)> m_SetColliderSnapStep;
            bool m_NeedsRebuild = false;
            bool m_MaterialSavePending = false;
            float m_MaterialSaveCountdown = 0.0f;
            static constexpr float MaterialSaveDelaySeconds = 0.35f;
            struct MaterialUndoRecord
            {
                std::filesystem::path Path;
                std::string Label;
                MaterialAsset Before;
                MaterialAsset After;
            };
            MaterialAsset m_MaterialUndoBaseline;
            bool m_HasMaterialUndoBaseline = false;
            std::vector<MaterialUndoRecord> m_MaterialUndoStack;
            std::vector<MaterialUndoRecord> m_MaterialRedoStack;
            static constexpr size_t MaxMaterialUndoRecords = 80;
        };

    }
}
