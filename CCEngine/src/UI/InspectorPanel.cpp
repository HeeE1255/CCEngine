#include "InspectorPanel.h"
#include "InspectorRegistry.h"
#include "InspectorUtils.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/MaterialPreviewRenderer.h"
#include "Renderer/Texture.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/PerspectiveCamera.h"
#include "Application.h"
#include "UI/Button.h"
#include "UI/ImageWidget.h"
#include "UI/Panel.h"
#include "UI/TextInput.h"
#include "Scene/Components.h"
#include "Renderer/MeshFactory.h"
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Scripting/ScriptCompiler.h"
#include "Scripting/ScriptMetadata.h"
#include "Utils/PlatformUtils.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace CCEngine
{
    namespace UI
    {
        namespace
        {
            DirectX::XMFLOAT4 ScaleColor(const DirectX::XMFLOAT4& color, float scale)
            {
                return {
                    (std::clamp)(color.x * scale, 0.0f, 1.0f),
                    (std::clamp)(color.y * scale, 0.0f, 1.0f),
                    (std::clamp)(color.z * scale, 0.0f, 1.0f),
                    color.w
                };
            }

            void DrawLowVertexMaterialSphere(float x, float y, float size, const DirectX::XMFLOAT4& albedo)
            {
                UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
                UIRenderer::DrawRect(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });

                float cx = x + size * 0.5f;
                float cy = y + size * 0.5f;
                float radius = size * 0.38f;
                constexpr int Bands = 12;
                for (int band = 0; band < Bands; ++band)
                {
                    float t0 = -1.0f + 2.0f * (float)band / (float)Bands;
                    float t1 = -1.0f + 2.0f * (float)(band + 1) / (float)Bands;
                    float mid = (t0 + t1) * 0.5f;
                    float halfWidth = std::sqrt((std::max)(0.0f, 1.0f - mid * mid)) * radius;
                    float bandY = cy + t0 * radius;
                    float bandH = (t1 - t0) * radius + 1.0f;
                    float shade = 0.50f + 0.42f * (1.0f - (mid + 0.25f) * (mid + 0.25f));
                    UIRenderer::DrawRectFilled(cx - halfWidth, bandY, halfWidth * 2.0f, bandH, ScaleColor(albedo, shade));
                }

                // 프리뷰는 값 확인용이므로 실제 렌더러 대신 저비용 밴드로 구를 흉내 낸다.
                // 드래그 중에도 디스크 저장이나 별도 렌더 타깃 생성이 일어나지 않게 하기 위한 장치다.
                UIRenderer::DrawRectFilled(cx - radius * 0.36f, cy - radius * 0.48f, radius * 0.44f, radius * 0.12f, ScaleColor(albedo, 1.35f));
                UIRenderer::DrawRectFilled(cx - radius * 0.48f, cy - radius * 0.30f, radius * 0.22f, radius * 0.08f, ScaleColor(albedo, 1.22f));
                UIRenderer::DrawRect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, { 0.04f, 0.04f, 0.045f, 0.75f });
            }

            class MaterialPreviewWidget : public Widget
            {
            public:
                MaterialPreviewWidget(const std::string& name, std::function<const MaterialAsset*()> getter)
                    : Widget(name), m_GetMaterial(std::move(getter))
                {
                }

                void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override
                {
                    m_CalculatedPos = { parentPos.x + 12.0f, parentPos.y + m_OffsetMin.y };
                    m_CalculatedSize = { (std::max)(120.0f, parentSize.x - 24.0f), 150.0f };
                }

                void OnRender() override
                {
                    if (!m_IsVisible)
                        return;

                    const MaterialAsset* material = m_GetMaterial ? m_GetMaterial() : nullptr;
                    DirectX::XMFLOAT4 albedo = material ? material->AlbedoColor : DirectX::XMFLOAT4{ 0.6f, 0.6f, 0.65f, 1.0f };
                    UIRenderer::DrawString("Preview", m_CalculatedPos.x, m_CalculatedPos.y + 16.0f, { 0.70f, 0.70f, 0.72f, 1.0f });
                    float previewSize = (std::min)(112.0f, (std::max)(72.0f, m_CalculatedSize.x - 24.0f));
                    float px = m_CalculatedPos.x + (m_CalculatedSize.x - previewSize) * 0.5f;
                    float py = m_CalculatedPos.y + 28.0f;
                    DrawLowVertexMaterialSphere(px, py, previewSize, albedo);
                }

            private:
                std::function<const MaterialAsset*()> m_GetMaterial;
            };

            Widget* FindVisibleDescendantByName(Widget* widget, const std::string& name)
            {
                if (!widget || !widget->IsVisible())
                    return nullptr;

                if (widget->GetName() == name)
                    return widget;

                for (Widget* child : widget->GetChildren())
                {
                    if (Widget* found = FindVisibleDescendantByName(child, name))
                        return found;
                }

                return nullptr;
            }

            std::filesystem::path GetMaterialPreviewDebugDirectory()
            {
                std::filesystem::path current = std::filesystem::current_path();
                std::filesystem::path localPath = current / "local" / "thumbnail_debug";
                if (std::filesystem::exists(localPath / "enable.txt"))
                    return localPath;

                std::filesystem::path parentLocalPath = current.parent_path() / "local" / "thumbnail_debug";
                if (std::filesystem::exists(parentLocalPath / "enable.txt"))
                    return parentLocalPath;

                return localPath;
            }

            bool IsMaterialPreviewDebugEnabled()
            {
                static const bool s_Enabled = std::filesystem::exists(GetMaterialPreviewDebugDirectory() / "enable.txt");
                return s_Enabled;
            }

            std::string SanitizeMaterialPreviewDebugName(std::string value)
            {
                for (char& c : value)
                {
                    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                        c = '_';
                }
                return value;
            }

            void AppendMaterialPreviewDebugLog(const std::string& message)
            {
                if (!IsMaterialPreviewDebugEnabled())
                    return;

                std::filesystem::create_directories(GetMaterialPreviewDebugDirectory());
                std::ofstream stream(GetMaterialPreviewDebugDirectory() / "trace.txt", std::ios::app);
                if (stream)
                    stream << message << "\n";
            }

            void WriteMaterialPreviewDebugBmp(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels)
            {
                if (width == 0 || height == 0 || pixels.size() != (size_t)width * (size_t)height)
                    return;

                std::ofstream stream(path, std::ios::binary);
                if (!stream)
                    return;

                const uint32_t bytesPerPixel = 4;
                const uint32_t imageBytes = width * height * bytesPerPixel;
                const uint32_t fileBytes = 14 + 40 + imageBytes;

                auto writeU16 = [&](uint16_t value)
                    {
                        stream.put((char)(value & 0xff));
                        stream.put((char)((value >> 8) & 0xff));
                    };

                auto writeU32 = [&](uint32_t value)
                    {
                        stream.put((char)(value & 0xff));
                        stream.put((char)((value >> 8) & 0xff));
                        stream.put((char)((value >> 16) & 0xff));
                        stream.put((char)((value >> 24) & 0xff));
                    };

                writeU16(0x4d42);
                writeU32(fileBytes);
                writeU16(0);
                writeU16(0);
                writeU32(14 + 40);

                writeU32(40);
                writeU32(width);
                writeU32(height);
                writeU16(1);
                writeU16(32);
                writeU32(0);
                writeU32(imageBytes);
                writeU32(2835);
                writeU32(2835);
                writeU32(0);
                writeU32(0);

                for (int y = (int)height - 1; y >= 0; --y)
                {
                    for (uint32_t x = 0; x < width; ++x)
                    {
                        uint32_t pixel = pixels[(size_t)y * (size_t)width + (size_t)x];
                        uint8_t r = (uint8_t)(pixel & 0xff);
                        uint8_t g = (uint8_t)((pixel >> 8) & 0xff);
                        uint8_t b = (uint8_t)((pixel >> 16) & 0xff);
                        uint8_t a = (uint8_t)((pixel >> 24) & 0xff);

                        stream.put((char)b);
                        stream.put((char)g);
                        stream.put((char)r);
                        stream.put((char)a);
                    }
                }
            }

            void DumpInspectorMaterialPreviewDebugImage(const std::filesystem::path& materialPath, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels)
            {
                if (!IsMaterialPreviewDebugEnabled())
                    return;

                static std::mutex s_DumpMutex;
                static std::unordered_set<std::string> s_DumpedLabels;

                std::string label = "inspector_preview_" + SanitizeMaterialPreviewDebugName(materialPath.filename().string());
                std::lock_guard<std::mutex> lock(s_DumpMutex);
                if (s_DumpedLabels.contains(label) || s_DumpedLabels.size() >= 4)
                    return;

                s_DumpedLabels.insert(label);
                std::filesystem::create_directories(GetMaterialPreviewDebugDirectory());

                std::ostringstream name;
                name << std::setw(2) << std::setfill('0') << s_DumpedLabels.size() << "_" << label << ".bmp";

                // 인스펙터 프리뷰가 실제로 읽어 낸 원본 픽셀을 그대로 저장한다.
                // 여기서 이미 회색이면 렌더 타깃 캡처 문제이고, 정상이면 이후 캐시/표시 단계 문제다.
                WriteMaterialPreviewDebugBmp(GetMaterialPreviewDebugDirectory() / name.str(), width, height, pixels);
                AppendMaterialPreviewDebugLog("inspector preview dumped: " + materialPath.string() +
                    " size=" + std::to_string(width) + "x" + std::to_string(height) +
                    " pixels=" + std::to_string(pixels.size()));
            }
        }

        InspectorPanel::InspectorPanel(const std::string& name, const std::string& title)
            : WindowPanel(name, title)
        {
            SetClipToBounds(true);
        }

        InspectorPanel::~InspectorPanel()
        {
            delete m_MaterialPreviewFramebuffer;
            m_MaterialPreviewFramebuffer = nullptr;
        }

        void InspectorPanel::SetSelectedEntity(Entity entity)
        {
            if (m_SelectedEntity == entity) return;

            FlushSelectedMaterialSave();
            m_SelectedEntity = entity;
            m_SelectedAssetPath.clear();
            m_SelectedAssetType.clear();
            m_MaterialPreviewImage = nullptr;
            RebuildInspector();
        }

        void InspectorPanel::SetSelectedAsset(const std::filesystem::path& assetPath, const std::string& assetType)
        {
            if (m_SelectedAssetPath == assetPath && m_SelectedAssetType == assetType)
                return;

            FlushSelectedMaterialSave();
            m_SelectedEntity = {};
            m_SelectedAssetPath = assetPath;
            m_SelectedAssetType = assetType;
            m_MaterialPreviewImage = nullptr;
            m_MaterialPreviewDirty = true;
            RebuildInspector();
        }

        bool InspectorPanel::ClearSelectedAssetIfMissing()
        {
            if (m_SelectedAssetPath.empty())
                return false;

            std::error_code ec;
            if (std::filesystem::exists(m_SelectedAssetPath, ec) && !ec)
                return false;

            // 선택한 에셋이 디스크에서 사라졌다면 더 이상 저장하면 안 된다.
            // 삭제 직후 남아 있던 디바운스 저장이 파일을 되살리는 상황을 여기서 끊는다.
            m_MaterialSavePending = false;
            m_MaterialSaveCountdown = 0.0f;
            m_SelectedAssetPath.clear();
            m_SelectedAssetType.clear();
            m_SelectedMaterial = MaterialAsset{};
            m_MaterialPreviewImage = nullptr;
            m_MaterialPreviewDirty = true;
            RebuildInspector();
            return true;
        }

        void InspectorPanel::RebuildInspector()
        {
            ClearChildren();
            m_AddComponentButton = nullptr;
            m_AddComponentMenu = nullptr;
            m_ComponentSearchInput = nullptr;
            m_ComponentButtons.clear();
            m_PreviousComponentPage = nullptr;
            m_ComponentPageLabel = nullptr;
            m_NextComponentPage = nullptr;
            m_ComponentFilter.clear();
            m_ComponentPage = 0;

            if (!m_SelectedAssetPath.empty())
            {
                if (m_SelectedAssetType == "material")
                    BuildMaterialInspector();
                return;
            }

            if (!m_SelectedEntity) return;

            InspectorRegistry::DrawAllComponents(this, m_SelectedEntity);

            m_AddComponentButton = new UI::Button("BtnAddComponent", "Add Component");
            m_AddComponentButton->SetOnClick([this]()
                {
                    if (!m_AddComponentMenu) return;
                    m_AddComponentMenu->SetVisible(!m_AddComponentMenu->IsVisible());
                    if (m_AddComponentMenu->IsVisible())
                    {
                        m_ComponentSearchInput->Clear();
                    }
                });
            AddChild(m_AddComponentButton);
            BuildAddComponentMenu();

            auto& window = CCEngine::Application::Get()->GetWindow();
            UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

        void InspectorPanel::BuildMaterialInspector()
        {
            if (!m_SelectedMaterial.LoadFromFile(m_SelectedAssetPath))
            {
                auto errorItem = new UI::InspectorItem("MaterialLoadError", "Material");
                errorItem->SetAnchorMin(0.0f, 0.0f);
                errorItem->SetAnchorMax(1.0f, 0.0f);
                auto errorButton = new UI::Button("MaterialLoadErrorText", "Failed to load material.");
                errorButton->SetNormalColor({ 0.20f, 0.08f, 0.08f, 1.0f });
                errorButton->SetHoverColor({ 0.20f, 0.08f, 0.08f, 1.0f });
                errorItem->AddChild(errorButton);
                AddChild(errorItem);
                return;
            }

            auto infoItem = new UI::InspectorItem("MaterialInfoItem", "Material Asset");
            infoItem->SetAnchorMin(0.0f, 0.0f);
            infoItem->SetAnchorMax(1.0f, 0.0f);
            AddChild(infoItem);

            auto nameInput = new UI::TextInput("MaterialNameInput", "Material Name");
            nameInput->SetText(m_SelectedMaterial.Name, false);
            nameInput->SetOnTextChanged([this](const std::string& text)
                {
                    m_SelectedMaterial.Name = text.empty() ? m_SelectedAssetPath.stem().string() : text;
                    MarkSelectedMaterialDirty();
                });
            infoItem->AddChild(nameInput);

            auto pathButton = new UI::Button("MaterialPathText", "File: " + m_SelectedAssetPath.filename().string());
            pathButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            pathButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(pathButton);

            auto shaderButton = new UI::Button("MaterialShaderButton", "Shader: " + m_SelectedMaterial.ShaderName);
            shaderButton->SetOnClick([this, shaderButton]()
                {
                    static const std::vector<std::string> shaders = { "Base3D", "Unlit", "Lit", "Transparent" };
                    auto it = std::find(shaders.begin(), shaders.end(), m_SelectedMaterial.ShaderName);
                    size_t nextIndex = it == shaders.end() ? 0 : ((size_t)std::distance(shaders.begin(), it) + 1) % shaders.size();
                    m_SelectedMaterial.ShaderName = shaders[nextIndex];
                    shaderButton->SetText("Shader: " + m_SelectedMaterial.ShaderName);
                    MarkSelectedMaterialDirty();
                });
            infoItem->AddChild(shaderButton);

            auto surfaceItem = new UI::InspectorItem("MaterialSurfaceItem", "Surface");
            surfaceItem->SetAnchorMin(0.0f, 0.0f);
            surfaceItem->SetAnchorMax(1.0f, 0.0f);
            AddChild(surfaceItem);

            // Material 에셋을 직접 편집한다. MeshComponent 값과 섞지 않아야 같은 재질을 쓰는 오브젝트들이 한 기준을 공유한다.
            InspectorUtils::AddColor4(surfaceItem, "MaterialAssetAlbedo", "Albedo",
                [this]() { return m_SelectedMaterial.AlbedoColor; },
                [this](DirectX::XMFLOAT4 value)
                {
                    m_SelectedMaterial.AlbedoColor = value;
                    MarkSelectedMaterialDirty();
                });

            auto albedoButton = new UI::Button("MaterialAlbedoTextureButton",
                m_SelectedMaterial.AlbedoTexturePath.empty()
                ? "Albedo Texture: (none)"
                : "Albedo Texture: " + std::filesystem::path(m_SelectedMaterial.AlbedoTexturePath).filename().string());
            albedoButton->SetOnClick([this, albedoButton]()
                {
                    std::string filepath = PlatformUtils::OpenFile("Texture (*.png;*.jpg;*.jpeg;*.tga)\0*.png;*.jpg;*.jpeg;*.tga\0");
                    if (filepath.empty())
                        return;

                    m_SelectedMaterial.AlbedoTexturePath = filepath;
                    m_SelectedMaterial.AlbedoTextureGuid = AssetDatabase::GetGuidFromPath(filepath);
                    m_SelectedMaterial.AlbedoTexture.reset(Texture2D::Create(filepath));
                    albedoButton->SetText("Albedo Texture: " + std::filesystem::path(filepath).filename().string());
                    MarkSelectedMaterialDirty();
                });
            surfaceItem->AddChild(albedoButton);

            auto clearAlbedoButton = new UI::Button("MaterialClearAlbedoTextureButton", "Clear Albedo Texture");
            clearAlbedoButton->SetOnClick([this, albedoButton]()
                {
                    m_SelectedMaterial.AlbedoTexturePath.clear();
                    m_SelectedMaterial.AlbedoTextureGuid.clear();
                    m_SelectedMaterial.AlbedoTexture.reset();
                    albedoButton->SetText("Albedo Texture: (none)");
                    MarkSelectedMaterialDirty();
                });
            surfaceItem->AddChild(clearAlbedoButton);

            auto normalButton = new UI::Button("MaterialNormalTextureButton",
                m_SelectedMaterial.NormalTexturePath.empty()
                ? "Normal Texture: (none)"
                : "Normal Texture: " + std::filesystem::path(m_SelectedMaterial.NormalTexturePath).filename().string());
            normalButton->SetOnClick([this, normalButton]()
                {
                    std::string filepath = PlatformUtils::OpenFile("Texture (*.png;*.jpg;*.jpeg;*.tga)\0*.png;*.jpg;*.jpeg;*.tga\0");
                    if (filepath.empty())
                        return;

                    m_SelectedMaterial.NormalTexturePath = filepath;
                    m_SelectedMaterial.NormalTextureGuid = AssetDatabase::GetGuidFromPath(filepath);
                    normalButton->SetText("Normal Texture: " + std::filesystem::path(filepath).filename().string());
                    MarkSelectedMaterialDirty();
                });
            surfaceItem->AddChild(normalButton);

            InspectorUtils::AddDragFloat(surfaceItem, "MaterialAssetRoughness", "Roughness",
                [this]() { return m_SelectedMaterial.Roughness; },
                [this](float value)
                {
                    m_SelectedMaterial.Roughness = std::clamp(value, 0.0f, 1.0f);
                    MarkSelectedMaterialDirty();
                });

            InspectorUtils::AddDragFloat(surfaceItem, "MaterialAssetMetallic", "Metallic",
                [this]() { return m_SelectedMaterial.Metallic; },
                [this](float value)
                {
                    m_SelectedMaterial.Metallic = std::clamp(value, 0.0f, 1.0f);
                    MarkSelectedMaterialDirty();
                });

            auto previewLabel = new UI::Button("MaterialPreviewLabel", "Preview");
            previewLabel->SetNormalColor({ 0.12f, 0.12f, 0.13f, 1.0f });
            previewLabel->SetHoverColor({ 0.12f, 0.12f, 0.13f, 1.0f });
            AddChild(previewLabel);

            EnsureMaterialPreviewResources();
            m_MaterialPreviewImage = new UI::ImageWidget("MaterialPreviewImage",
                m_MaterialPreviewFramebuffer ? m_MaterialPreviewFramebuffer->GetColorAttachmentRendererID(0) : nullptr);
            m_MaterialPreviewImage->SetAnchorMin(0.0f, 0.0f);
            m_MaterialPreviewImage->SetAnchorMax(1.0f, 0.0f);
            AddChild(m_MaterialPreviewImage);

            auto& window = CCEngine::Application::Get()->GetWindow();
            UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

        void InspectorPanel::MarkSelectedMaterialDirty()
        {
            if (m_SelectedAssetPath.empty() || m_SelectedAssetType != "material")
                return;

            // 편집 중에는 씬에 먼저 메모리 값을 흘려보내고 저장은 잠시 늦춘다.
            // 색 슬라이더처럼 연속으로 값이 바뀌는 UI에서 파일 IO와 참조 검증이 매번 돌면 호버/드래그가 끊긴다.
            if (m_OnMaterialPreviewChanged)
                m_OnMaterialPreviewChanged(m_SelectedAssetPath, m_SelectedMaterial);

            m_MaterialPreviewDirty = true;
            m_MaterialSavePending = true;
            m_MaterialSaveCountdown = MaterialSaveDelaySeconds;
        }

        void InspectorPanel::EnsureMaterialPreviewResources()
        {
            if (!m_MaterialPreviewFramebuffer)
            {
                FramebufferSpecification spec;
                spec.Width = 384;
                spec.Height = 384;
                m_MaterialPreviewFramebuffer = Framebuffer::Create(spec);
            }

            if (!m_MaterialPreviewMesh)
                m_MaterialPreviewMesh = MeshFactory::CreateSphere(0.75f, 32, 16);
        }

        void InspectorPanel::RenderSelectedMaterialPreview()
        {
            if (m_SelectedAssetType != "material" || m_SelectedAssetPath.empty())
                return;

            EnsureMaterialPreviewResources();
            if (!m_MaterialPreviewFramebuffer || !m_MaterialPreviewMesh)
                return;

            if (!m_MaterialPreviewDirty && !m_IsDraggingMaterialPreview)
                return;

            auto& window = CCEngine::Application::Get()->GetWindow();
            MaterialPreviewRenderOptions options;
            options.Yaw = m_MaterialPreviewYaw;
            options.Pitch = m_MaterialPreviewPitch;
            options.TargetWidth = m_MaterialPreviewFramebuffer->GetSpecification().Width;
            options.TargetHeight = m_MaterialPreviewFramebuffer->GetSpecification().Height;
            options.RestoreViewportWidth = window.GetWidth();
            options.RestoreViewportHeight = window.GetHeight();
            RenderMaterialPreviewToFramebuffer(m_MaterialPreviewFramebuffer, m_MaterialPreviewMesh, m_SelectedMaterial, options);

            if (m_MaterialPreviewImage)
                m_MaterialPreviewImage->SetTexture(m_MaterialPreviewFramebuffer->GetColorAttachmentRendererID(0));

            if (m_OnMaterialPreviewTextureReady)
            {
                // 임시 썸네일 브리지:
                // 인스펙터 프리뷰에 실제로 표시된 렌더 타깃을 브라우저가 그대로 그려 보게 한다.
                // 이 경로가 보이면 문제는 픽셀 캡처/Texture2D 재생성 쪽으로 좁혀진다.
                m_OnMaterialPreviewTextureReady(m_SelectedAssetPath, m_MaterialPreviewFramebuffer->GetColorAttachmentRendererID(0));
            }

            if (m_OnMaterialPreviewCaptured && !m_IsDraggingMaterialPreview)
            {
                std::vector<uint32_t> pixels;
                if (m_MaterialPreviewFramebuffer->ReadColorPixels(pixels))
                {
                    const FramebufferSpecification& spec = m_MaterialPreviewFramebuffer->GetSpecification();
                    DumpInspectorMaterialPreviewDebugImage(m_SelectedAssetPath, spec.Width, spec.Height, pixels);
                    // 브라우저 썸네일은 사용자가 실제로 보고 있는 프리뷰 결과를 복사해 쓴다.
                    // 별도 숨은 렌더러가 실패해도 인스펙터와 브라우저 표시가 서로 어긋나지 않는다.
                    m_OnMaterialPreviewCaptured(m_SelectedAssetPath, spec.Width, spec.Height, pixels);
                }
            }

            m_MaterialPreviewDirty = false;
        }

        bool InspectorPanel::IsMaterialPreviewPoint(float mouseX, float mouseY) const
        {
            return m_MaterialPreviewImage &&
                m_MaterialPreviewImage->IsVisible() &&
                m_MaterialPreviewImage->IsPointInside(mouseX, mouseY);
        }

        void InspectorPanel::FlushSelectedMaterialSave()
        {
            if (!m_MaterialSavePending)
                return;

            SaveSelectedMaterial();
            m_MaterialSavePending = false;
            m_MaterialSaveCountdown = 0.0f;
        }

        void InspectorPanel::SaveSelectedMaterial()
        {
            if (m_SelectedAssetPath.empty() || m_SelectedAssetType != "material")
                return;

            if (!m_SelectedMaterial.SaveToFile(m_SelectedAssetPath))
            {
                ConsoleLog::Error("Failed to save material: " + m_SelectedAssetPath.string());
                return;
            }

            // 저장 직후 meta를 보장한다. 에디터에서 만든 Material은 곧바로 씬/프리팹에서 GUID 참조가 가능해야 한다.
            AssetDatabase::EnsureMetaFile(m_SelectedAssetPath);
            AssetDatabase::MarkDirty(m_SelectedAssetPath.parent_path());
            if (m_OnAssetChanged)
                m_OnAssetChanged(m_SelectedAssetPath, m_SelectedAssetType);
        }

        void InspectorPanel::BuildAddComponentMenu()
        {
            m_AddComponentMenu = new UI::Panel("AddComponentMenu", { 0.14f, 0.14f, 0.15f, 1.0f });
            m_AddComponentMenu->SetVisible(false);
            m_AddComponentMenu->SetAnchorMin(0.0f, 0.0f);
            m_AddComponentMenu->SetAnchorMax(0.0f, 0.0f);
            AddChild(m_AddComponentMenu);

            m_ComponentSearchInput = new UI::TextInput("ComponentSearch", "Search components...");
            m_ComponentSearchInput->SetAnchorMin(0.0f, 0.0f);
            m_ComponentSearchInput->SetAnchorMax(1.0f, 0.0f);
            m_ComponentSearchInput->SetOffsetMin(6.0f, 6.0f);
            m_ComponentSearchInput->SetOffsetMax(-6.0f, 32.0f);
            m_ComponentSearchInput->SetOnTextChanged([this](const std::string& query)
                {
                    m_ComponentPage = 0;
                    FilterAddComponentMenu(query);
                });
            m_AddComponentMenu->AddChild(m_ComponentSearchInput);

            auto addCandidate = [this](const std::string& name, AddComponentType type, bool alreadyExists)
                {
                    if (alreadyExists) return;
                    auto button = new UI::Button("Add" + name, name);
                    button->SetOnClick([this, type]() { AddComponent(type); });
                    m_AddComponentMenu->AddChild(button);
                    m_ComponentButtons.emplace_back(button, name);
                };

            addCandidate("Mesh Renderer", AddComponentType::Mesh, m_SelectedEntity.HasComponent<MeshComponent>());
            addCandidate("Light", AddComponentType::Light, m_SelectedEntity.HasComponent<LightComponent>());
            addCandidate("Camera", AddComponentType::Camera, m_SelectedEntity.HasComponent<CameraComponent>());
            addCandidate("Sprite Renderer", AddComponentType::SpriteRenderer, m_SelectedEntity.HasComponent<SpriteRendererComponent>());
            addCandidate("Rigidbody 2D", AddComponentType::Rigidbody2D, m_SelectedEntity.HasComponent<Rigidbody2DComponent>());
            addCandidate("Box Collider 2D", AddComponentType::BoxCollider2D, m_SelectedEntity.HasComponent<BoxCollider2DComponent>());
            addCandidate("Box Collider 3D", AddComponentType::BoxCollider3D, m_SelectedEntity.HasComponent<BoxCollider3DComponent>());
            addCandidate("Sphere Collider 3D", AddComponentType::SphereCollider3D, m_SelectedEntity.HasComponent<SphereCollider3DComponent>());
            addCandidate("Cylinder Collider 3D", AddComponentType::CylinderCollider3D, m_SelectedEntity.HasComponent<CylinderCollider3DComponent>());
            addCandidate("Mesh Collider 3D", AddComponentType::MeshCollider3D, m_SelectedEntity.HasComponent<MeshCollider3DComponent>());
            addCandidate("New C# Script...", AddComponentType::Script, m_SelectedEntity.HasComponent<ScriptComponent>());

            if (!m_SelectedEntity.HasComponent<ScriptComponent>())
            {
                for (const std::string& className : DiscoverScriptClasses())
                {
                    auto button = new UI::Button("Attach" + className, className);
                    button->SetOnClick([this, className]() { AttachExistingScript(className); });
                    m_AddComponentMenu->AddChild(button);
                    m_ComponentButtons.emplace_back(button, "C# Script " + className);
                }
            }

            m_PreviousComponentPage = new UI::Button("PreviousComponentPage", "<");
            m_PreviousComponentPage->SetOnClick([this]() { ChangeComponentPage(-1); });
            m_AddComponentMenu->AddChild(m_PreviousComponentPage);

            m_ComponentPageLabel = new UI::Button("ComponentPageLabel", "1 / 1");
            m_AddComponentMenu->AddChild(m_ComponentPageLabel);

            m_NextComponentPage = new UI::Button("NextComponentPage", ">");
            m_NextComponentPage->SetOnClick([this]() { ChangeComponentPage(1); });
            m_AddComponentMenu->AddChild(m_NextComponentPage);
            FilterAddComponentMenu("");
        }

        void InspectorPanel::AddComponent(AddComponentType type)
        {
            if (!m_SelectedEntity) return;

            auto componentName = [](AddComponentType componentType)
            {
                switch (componentType)
                {
                    case AddComponentType::Mesh: return "Mesh Renderer";
                    case AddComponentType::Light: return "Light";
                    case AddComponentType::Camera: return "Camera";
                    case AddComponentType::SpriteRenderer: return "Sprite Renderer";
                    case AddComponentType::Rigidbody2D: return "Rigidbody 2D";
                    case AddComponentType::BoxCollider2D: return "Box Collider 2D";
                    case AddComponentType::BoxCollider3D: return "Box Collider 3D";
                    case AddComponentType::SphereCollider3D: return "Sphere Collider 3D";
                    case AddComponentType::CylinderCollider3D: return "Cylinder Collider 3D";
                    case AddComponentType::MeshCollider3D: return "Mesh Collider 3D";
                    case AddComponentType::Script: return "C# Script";
                    default: return "Component";
                }
            };

            if (type == AddComponentType::Script)
            {
                m_AddComponentMenu->SetVisible(false);
                if (CreateAndAttachScript())
                {
                    CommitStructureChange();
                    RequestRebuild();
                }
                return;
            }

            // 컴포넌트 추가도 Undo/Redo 대상이라 변경 전후를 에디터 레이어에 알려준다.
            BeginStructureChange(std::string("Add ") + componentName(type));
            switch (type)
            {
                case AddComponentType::Mesh:
                {
                    auto& mesh = m_SelectedEntity.AddComponent<MeshComponent>(MeshComponent::MeshType::Cube);
                    mesh.MeshData = MeshFactory::CreateCube();
                    break;
                }
                case AddComponentType::Light: m_SelectedEntity.AddComponent<LightComponent>(); break;
                case AddComponentType::Camera:
                {
                    auto& camera = m_SelectedEntity.AddComponent<CameraComponent>();
                    auto view = m_SelectedEntity.GetScene()->GetRegistry().view<CameraComponent>();
                    bool hasOtherPrimary = false;
                    for (auto entity : view)
                    {
                        if (entity != (entt::entity)m_SelectedEntity && view.get<CameraComponent>(entity).Primary)
                        {
                            hasOtherPrimary = true;
                            break;
                        }
                    }
                    camera.Primary = !hasOtherPrimary;
                    break;
                }
                case AddComponentType::SpriteRenderer: m_SelectedEntity.AddComponent<SpriteRendererComponent>(); break;
                case AddComponentType::Rigidbody2D: m_SelectedEntity.AddComponent<Rigidbody2DComponent>(); break;
                case AddComponentType::BoxCollider2D: m_SelectedEntity.AddComponent<BoxCollider2DComponent>(); break;
                case AddComponentType::BoxCollider3D: m_SelectedEntity.AddComponent<BoxCollider3DComponent>(); break;
                case AddComponentType::SphereCollider3D: m_SelectedEntity.AddComponent<SphereCollider3DComponent>(); break;
                case AddComponentType::CylinderCollider3D: m_SelectedEntity.AddComponent<CylinderCollider3DComponent>(); break;
                case AddComponentType::MeshCollider3D: m_SelectedEntity.AddComponent<MeshCollider3DComponent>(); break;
                case AddComponentType::Script: break;
            }

            m_AddComponentMenu->SetVisible(false);
            CommitStructureChange();
            RequestRebuild();
        }

        bool InspectorPanel::CreateAndAttachScript()
        {
            if (!m_SelectedEntity || m_SelectedEntity.GetScene()->GetState() != SceneState::Edit)
            {
                ConsoleLog::Warning("Stop Play Mode before creating a C# script.");
                return false;
            }

            const auto scriptsDirectory = std::filesystem::current_path() / "assets" / "Scripts" / "Game";
            std::error_code ec;
            std::filesystem::create_directories(scriptsDirectory, ec);
            if (ec)
            {
                ConsoleLog::Error("Failed to create the script directory: " + scriptsDirectory.string());
                return false;
            }

            const std::string initialDirectory = scriptsDirectory.string();
            std::string filepath = PlatformUtils::SaveFile(
                "C# Script (*.cs)\0*.cs\0",
                initialDirectory.c_str());
            if (filepath.empty())
                return false;

            std::filesystem::path scriptPath = std::filesystem::absolute(filepath).lexically_normal();
            std::filesystem::path assetsRoot = std::filesystem::absolute(
                std::filesystem::current_path() / "assets").lexically_normal();
            std::filesystem::path relative = std::filesystem::relative(scriptPath, assetsRoot, ec);
            if (ec || relative.empty() || relative.begin()->string() == "..")
            {
                ConsoleLog::Error("C# scripts must be saved inside the project assets folder.");
                return false;
            }

            std::string className = scriptPath.stem().string();
            className.erase(
                std::remove_if(className.begin(), className.end(),
                    [](unsigned char c) { return !std::isalnum(c) && c != '_'; }),
                className.end());
            if (className.empty())
                className = "NewScript";
            if (std::isdigit(static_cast<unsigned char>(className.front())))
                className.insert(className.begin(), '_');

            std::ofstream output(scriptPath, std::ios::trunc);
            if (!output.is_open())
            {
                ConsoleLog::Error("Failed to create C# script: " + scriptPath.string());
                return false;
            }

            output <<
                "using CCEngine;\n\n"
                "namespace Game\n"
                "{\n"
                "    public sealed class " << className << " : GameScript\n"
                "    {\n"
                "        // [Range(0.0f, 10.0f)]처럼 붙이면 인스펙터 표시 방식이 바뀝니다.\n"
                "        public float Speed = 1.0f;\n\n"
                "        protected override void Awake()\n"
                "        {\n"
                "            // Play가 시작되어 스크립트 인스턴스가 준비될 때 한 번 호출됩니다.\n"
                "        }\n\n"
                "        protected override void Start()\n"
                "        {\n"
                "            // 모든 Awake와 OnEnable이 끝난 뒤, 첫 Update 전에 한 번 호출됩니다.\n"
                "        }\n\n"
                "        protected override void Update(float deltaTime)\n"
                "        {\n"
                "            // Play 중 매 프레임 호출되며 deltaTime은 이전 프레임부터 흐른 시간입니다.\n"
                "        }\n\n"
                "        protected override void OnDestroy()\n"
                "        {\n"
                "            // Play가 끝나거나 스크립트 인스턴스가 제거될 때 한 번 호출됩니다.\n"
                "        }\n"
                "    }\n"
                "}\n";
            output.close();

            BeginStructureChange("Add C# Script");
            m_SelectedEntity.GetScene()->AddScriptComponent(m_SelectedEntity, "Game." + className, true);

            AssetDatabase::EnsureMetaFile(scriptPath);
            AssetDatabase::MarkDirty(assetsRoot);
            ScriptCompiler::RequestCompile();
            ConsoleLog::Info("C# script created and attached: " + scriptPath.string());
            return true;
        }

        void InspectorPanel::AttachExistingScript(const std::string& className)
        {
            if (!m_SelectedEntity || m_SelectedEntity.HasComponent<ScriptComponent>())
                return;

            Scene* scene = m_SelectedEntity.GetScene();
            const bool editMode = scene && scene->GetState() == SceneState::Edit;

            if (editMode)
                BeginStructureChange("Add C# Script");

            // 기존 스크립트 부착은 Play 중에도 허용한다.
            // 런타임 씬에 붙이면 즉시 Awake/OnEnable 준비를 하고, Stop하면 에디터 원본 씬에는 남지 않는다.
            scene->AddScriptComponent(m_SelectedEntity, className, true);

            if (editMode)
                CommitStructureChange();

            m_AddComponentMenu->SetVisible(false);
            ScriptCompiler::RequestCompile();
            ConsoleLog::Info("C# script attached: " + className);
            RequestRebuild();
        }

        std::vector<std::string> InspectorPanel::DiscoverScriptClasses() const
        {
            // 메뉴에는 컴파일된 DLL에서 검증된 스크립트만 보여준다.
            // 소스 파싱은 주석, partial class, 네임스페이스 형식에 따라 쉽게 틀어진다.
            ScriptMetadata::Refresh();
            return ScriptMetadata::GetClassNames();
        }

        void InspectorPanel::BeginStructureChange(const std::string& label)
        {
            // 인스펙터는 Undo 스택을 직접 들고 있지 않고, 변경 시작만 에디터 레이어에 전달한다.
            if (m_BeginStructureChange)
                m_BeginStructureChange(label);
        }

        void InspectorPanel::CommitStructureChange()
        {
            // 실제 컴포넌트 변경이 끝난 뒤 호출해야 Before/After 스냅샷이 나뉜다.
            if (m_CommitStructureChange)
                m_CommitStructureChange();
        }

        bool InspectorPanel::IsAlbedoTextureSlotPoint(float mouseX, float mouseY) const
        {
            Entity selected = m_SelectedEntity;
            if (!selected || !selected.HasComponent<MeshComponent>())
                return false;

            // Mesh Renderer 안의 텍스처 슬롯만 드롭 대상으로 본다.
            // 인스펙터 빈 공간까지 허용하면 다른 항목 위에서 놓았을 때 의도치 않게 재질이 바뀐다.
            Widget* slot = FindVisibleDescendantByName(const_cast<InspectorPanel*>(this), "BtnChangeTexture");
            return slot && slot->IsPointInside(mouseX, mouseY);
        }

        bool InspectorPanel::IsMaterialSlotPoint(float mouseX, float mouseY) const
        {
            Entity selected = m_SelectedEntity;
            if (!selected || !selected.HasComponent<MeshComponent>())
                return false;

            // Material 슬롯도 정확한 버튼 영역만 드롭 대상으로 삼는다.
            // 인스펙터 위에 올렸다는 이유만으로 재질이 바뀌면 상용 툴에서 치명적인 오조작이 된다.
            Widget* slot = FindVisibleDescendantByName(const_cast<InspectorPanel*>(this), "BtnChangeMaterial");
            return slot && slot->IsPointInside(mouseX, mouseY);
        }

        void InspectorPanel::FilterAddComponentMenu(const std::string& query)
        {
            m_ComponentFilter = query;
            std::string lowerQuery = query;
            std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });

            std::vector<Button*> matches;
            for (auto& [button, name] : m_ComponentButtons)
            {
                std::string lowerName = name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                bool visible = lowerQuery.empty() || lowerName.find(lowerQuery) != std::string::npos;
                button->SetVisible(false);
                if (visible)
                    matches.push_back(button);
            }

            const size_t pageCount = (std::max<size_t>)(1, (matches.size() + ComponentPageSize - 1) / ComponentPageSize);
            m_ComponentPage = (std::min)(m_ComponentPage, pageCount - 1);
            const size_t first = m_ComponentPage * ComponentPageSize;
            const size_t last = (std::min)(first + ComponentPageSize, matches.size());

            float currentY = 38.0f;
            for (size_t index = first; index < last; ++index)
            {
                Button* button = matches[index];
                button->SetVisible(true);
                button->SetAnchorMin(0.0f, 0.0f);
                button->SetAnchorMax(1.0f, 0.0f);
                button->SetOffsetMin(6.0f, currentY);
                button->SetOffsetMax(-6.0f, currentY + 26.0f);
                currentY += 28.0f;
            }

            const bool showPager = pageCount > 1;
            if (m_PreviousComponentPage && m_ComponentPageLabel && m_NextComponentPage)
            {
                m_PreviousComponentPage->SetVisible(showPager);
                m_ComponentPageLabel->SetVisible(showPager);
                m_NextComponentPage->SetVisible(showPager);
                if (showPager)
                {
                    m_PreviousComponentPage->SetAnchorMin(0.0f, 0.0f);
                    m_PreviousComponentPage->SetAnchorMax(0.0f, 0.0f);
                    m_PreviousComponentPage->SetOffsetMin(6.0f, currentY);
                    m_PreviousComponentPage->SetOffsetMax(42.0f, currentY + 26.0f);

                    m_ComponentPageLabel->SetAnchorMin(0.0f, 0.0f);
                    m_ComponentPageLabel->SetAnchorMax(1.0f, 0.0f);
                    m_ComponentPageLabel->SetOffsetMin(46.0f, currentY);
                    m_ComponentPageLabel->SetOffsetMax(-46.0f, currentY + 26.0f);
                    m_ComponentPageLabel->SetText(
                        std::to_string(m_ComponentPage + 1) + " / " + std::to_string(pageCount));

                    m_NextComponentPage->SetAnchorMin(1.0f, 0.0f);
                    m_NextComponentPage->SetAnchorMax(1.0f, 0.0f);
                    m_NextComponentPage->SetOffsetMin(-42.0f, currentY);
                    m_NextComponentPage->SetOffsetMax(-6.0f, currentY + 26.0f);
                    currentY += 30.0f;
                }
            }
            if (m_AddComponentMenu)
                m_AddComponentMenu->SetSize(m_AddComponentMenu->GetSize().x, currentY + 6.0f);
        }

        void InspectorPanel::ChangeComponentPage(int direction)
        {
            if (direction < 0 && m_ComponentPage > 0)
                --m_ComponentPage;
            else if (direction > 0)
                ++m_ComponentPage;
            FilterAddComponentMenu(m_ComponentFilter);
        }

        void InspectorPanel::OnUpdate(float deltaTime)
        {
            WindowPanel::OnUpdate(deltaTime);
            RenderSelectedMaterialPreview();
            if (m_MaterialSavePending)
            {
                m_MaterialSaveCountdown -= deltaTime;
                if (m_MaterialSaveCountdown <= 0.0f)
                    FlushSelectedMaterialSave();
            }

            if (m_NeedsRebuild)
            {
                FlushSelectedMaterialSave();
                m_NeedsRebuild = false;
                RebuildInspector();
            }
        }

        void InspectorPanel::OnRender()
        {
            if (!m_IsVisible) return;

            float rightPadding = (m_ScrollState.GetMaxScroll() > 0) ? 22.0f : 0.0f;
            SetClipPadding(0.0f, 40.0f, rightPadding, 0.0f);

            UI::WindowPanel::OnRender();

            // 스크롤바 그리기
            if (m_ScrollState.GetMaxScroll() > 0)
            {
                float thumbH = m_ScrollState.GetThumbHeight();
                float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + 40.0f);
                float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;

                UIRenderer::DrawRect({ thumbX, m_CalculatedPos.y + 40.0f }, { 8.0f, m_ScrollState.ViewportHeight }, { 0.1f, 0.1f, 0.1f, 0.5f });
                UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.4f, 0.4f, 0.4f, 1.0f });
            }
        }

        bool InspectorPanel::OnEvent(Event& e)
        {
            if (!m_IsVisible)
                return false;

            if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                auto& me = static_cast<MouseButtonPressedEvent&>(e);
                if (me.GetButton() == 0 && IsMaterialPreviewPoint(me.GetX(), me.GetY()))
                {
                    m_IsDraggingMaterialPreview = true;
                    m_MaterialPreviewDragStartX = me.GetX();
                    m_MaterialPreviewDragStartY = me.GetY();
                    m_MaterialPreviewDragStartYaw = m_MaterialPreviewYaw;
                    m_MaterialPreviewDragStartPitch = m_MaterialPreviewPitch;
                    Widget::BeginMouseInteraction(this);
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_ScrollState.GetMaxScroll() > 0.0f)
                {
                    float thumbH = m_ScrollState.GetThumbHeight();
                    float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + 40.0f);
                    float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;
                    bool onThumb = me.GetX() >= thumbX && me.GetX() <= thumbX + 8.0f &&
                        me.GetY() >= thumbY && me.GetY() <= thumbY + thumbH;
                    if (onThumb)
                    {
                        m_IsDraggingScrollbar = true;
                        m_DragMouseStartY = me.GetY();
                        m_DragScrollStartY = m_ScrollState.ScrollY;
                        Widget::BeginMouseInteraction(this);
                        e.Handled = true;
                        return true;
                    }
                }
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                m_ScrollState.SetFromThumbDrag(me.GetY(), m_DragMouseStartY, m_DragScrollStartY);
                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingMaterialPreview)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                m_MaterialPreviewYaw = m_MaterialPreviewDragStartYaw + (me.GetX() - m_MaterialPreviewDragStartX) * 0.012f;
                m_MaterialPreviewPitch = (std::clamp)(
                    m_MaterialPreviewDragStartPitch + (me.GetY() - m_MaterialPreviewDragStartY) * 0.012f,
                    -1.25f,
                    1.25f);
                m_MaterialPreviewDirty = true;
                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseButtonReleased)
            {
                auto& me = static_cast<MouseButtonReleasedEvent&>(e);
                if (me.GetButton() == 0 && m_IsDraggingMaterialPreview)
                {
                    m_IsDraggingMaterialPreview = false;
                    Widget::EndMouseInteraction(this);
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_IsDraggingScrollbar)
                {
                    m_IsDraggingScrollbar = false;
                    Widget::EndMouseInteraction(this);
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseScrolled)
            {
                auto& se = static_cast<MouseScrolledEvent&>(e);
                auto [mouseX, mouseY] = CCEngine::Application::Get()->GetWindow().GetMousePosition();
                if (IsPointInside(mouseX, mouseY))
                {
                    m_ScrollState.ApplyScroll(se.GetYOffset() * -1.0f);
                    e.Handled = true;
                    return true;
                }
            }

            return WindowPanel::OnEvent(e);
        }

        void InspectorPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            WindowPanel::UpdateLayout(parentPos, parentSize);

            if (!m_IsVisible || (!m_SelectedEntity && m_SelectedAssetPath.empty())) return;

            float startY = 40.0f;
            float currentY = startY - m_ScrollState.ScrollY;

            for (auto child : m_Children)
            {
                if (!child->IsVisible()) continue;
                if (child == m_AddComponentMenu) continue;

                if (child == m_AddComponentButton)
                {
                    child->SetAnchorMin(0.0f, 0.0f);
                    child->SetAnchorMax(1.0f, 0.0f);
                    child->SetOffsetMin(12.0f, currentY);
                    child->SetOffsetMax(-12.0f, currentY + 30.0f);
                    child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
                    currentY += 34.0f;
                    continue;
                }

                if (child->GetName() == "MaterialPreviewLabel")
                {
                    child->SetAnchorMin(0.0f, 0.0f);
                    child->SetAnchorMax(1.0f, 0.0f);
                    child->SetOffsetMin(12.0f, currentY);
                    child->SetOffsetMax(-12.0f, currentY + 24.0f);
                    child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
                    currentY += 28.0f;
                    continue;
                }

                if (child == m_MaterialPreviewImage)
                {
                    float previewSize = (std::min)(180.0f, (std::max)(96.0f, m_CalculatedSize.x - 36.0f));
                    float previewX = (m_CalculatedSize.x - previewSize) * 0.5f;
                    child->SetAnchorMin(0.0f, 0.0f);
                    child->SetAnchorMax(0.0f, 0.0f);
                    child->SetOffsetMin(previewX, currentY);
                    child->SetOffsetMax(previewX + previewSize, currentY + previewSize);
                    child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
                    currentY += previewSize + 10.0f;
                    continue;
                }

                child->SetOffsetMin(0.0f, currentY);
                child->SetOffsetMax(0.0f, currentY);
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);

                currentY += child->GetCalculatedSize().y + 4.0f;
            }

            m_ScrollState.ContentHeight = (currentY + m_ScrollState.ScrollY) - startY;
            m_ScrollState.ViewportHeight = m_CalculatedSize.y - startY;

            if (m_AddComponentMenu && m_AddComponentButton)
            {
                auto buttonPos = m_AddComponentButton->GetCalculatedPosition();
                float menuWidth = (std::max)(180.0f, m_CalculatedSize.x - 24.0f);
                float menuHeight = m_AddComponentMenu->GetSize().y;
                float localX = 12.0f;
                float localY = buttonPos.y - m_CalculatedPos.y + 34.0f;
                if (localY + menuHeight > m_CalculatedSize.y)
                    localY = (std::max)(40.0f, buttonPos.y - m_CalculatedPos.y - menuHeight);
                m_AddComponentMenu->SetOffsetMin(localX, localY);
                m_AddComponentMenu->SetOffsetMax(localX + menuWidth, localY + menuHeight);
                m_AddComponentMenu->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
            }
        }

    }
}
