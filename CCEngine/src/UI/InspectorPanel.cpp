#include "InspectorPanel.h"
#include "InspectorRegistry.h"
#include "InspectorUtils.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/MaterialPreviewRenderer.h"
#include "Renderer/RuntimeShaderLibrary.h"
#include "Renderer/ShaderAsset.h"
#include "Renderer/ShaderCompiler.h"
#include "Renderer/Texture.h"
#include "Renderer/VisualShaderAsset.h"
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
#include "Events/KeyEvent.h"
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
#include <Windows.h>

namespace CCEngine
{
    namespace UI
    {
        namespace
        {
            std::mutex s_MaterialPreviewDebugDumpMutex;
            std::unordered_set<std::string> s_MaterialPreviewDebugDumpedLabels;

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

            DirectX::XMFLOAT4 GetShaderStatusColor(const ShaderCacheStatus& cacheStatus, const RuntimeShaderStatus& runtimeStatus)
            {
                if (runtimeStatus.HasEntry && runtimeStatus.UsingErrorShader)
                    return { 0.34f, 0.08f, 0.12f, 1.0f };

                if (!cacheStatus.IsHlsl)
                    return { 0.17f, 0.16f, 0.11f, 1.0f };

                if (!cacheStatus.SourceExists)
                    return { 0.34f, 0.08f, 0.12f, 1.0f };

                if (cacheStatus.VertexBytecodeFresh && cacheStatus.PixelBytecodeFresh)
                    return { 0.10f, 0.22f, 0.13f, 1.0f };

                return { 0.30f, 0.22f, 0.08f, 1.0f };
            }

            std::string BuildShaderStatusText(const std::filesystem::path& shaderPath)
            {
                ShaderCacheStatus cacheStatus = ShaderCompiler::GetHlslCacheStatus(shaderPath);
                RuntimeShaderStatus runtimeStatus = RuntimeShaderLibrary::GetStatusForShader(shaderPath);

                if (runtimeStatus.HasEntry && runtimeStatus.UsingErrorShader)
                    return "Shader Status: Error Shader active";

                ShaderCompileResult lastCompile;
                if (ShaderCompiler::GetLastCompileResult(shaderPath, lastCompile) && !lastCompile.Success)
                {
                    if (!lastCompile.ReflectionErrors.empty())
                        return "Shader Status: reflection validation failed";
                    return "Shader Status: last compile failed";
                }

                if (!cacheStatus.IsHlsl)
                    return "Shader Status: Legacy metadata";

                return "Shader Status: " + cacheStatus.Message;
            }

            void ApplyShaderStatusButtonStyle(Button* button, const std::filesystem::path& shaderPath)
            {
                if (!button)
                    return;

                ShaderCacheStatus cacheStatus = ShaderCompiler::GetHlslCacheStatus(shaderPath);
                RuntimeShaderStatus runtimeStatus = RuntimeShaderLibrary::GetStatusForShader(shaderPath);
                ShaderCompileResult lastCompile;
                DirectX::XMFLOAT4 color =
                    (ShaderCompiler::GetLastCompileResult(shaderPath, lastCompile) && !lastCompile.Success)
                    ? DirectX::XMFLOAT4{ 0.34f, 0.08f, 0.12f, 1.0f }
                    : GetShaderStatusColor(cacheStatus, runtimeStatus);
                button->SetNormalColor(color);
                button->SetHoverColor(ScaleColor(color, 1.18f));
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

                std::string label = "inspector_preview_" + SanitizeMaterialPreviewDebugName(materialPath.filename().string());
                std::lock_guard<std::mutex> lock(s_MaterialPreviewDebugDumpMutex);
                if (s_MaterialPreviewDebugDumpedLabels.contains(label) || s_MaterialPreviewDebugDumpedLabels.size() >= 4)
                    return;

                s_MaterialPreviewDebugDumpedLabels.insert(label);
                std::filesystem::create_directories(GetMaterialPreviewDebugDirectory());

                std::ostringstream name;
                name << std::setw(2) << std::setfill('0') << s_MaterialPreviewDebugDumpedLabels.size() << "_" << label << ".bmp";

                // 인스펙터 프리뷰가 실제로 읽어 낸 원본 픽셀을 그대로 저장한다.
                // 여기서 이미 회색이면 렌더 타깃 캡처 문제이고, 정상이면 이후 캐시/표시 단계 문제다.
                WriteMaterialPreviewDebugBmp(GetMaterialPreviewDebugDirectory() / name.str(), width, height, pixels);
                AppendMaterialPreviewDebugLog("inspector preview dumped: " + materialPath.string() +
                    " size=" + std::to_string(width) + "x" + std::to_string(height) +
                    " pixels=" + std::to_string(pixels.size()));
            }

            bool IsSurfaceBackedShaderProperty(const ShaderPropertyDefinition& definition)
            {
                if (definition.Name == "AlbedoColor" && definition.Type == ShaderPropertyType::Color)
                    return true;
                if ((definition.Name == "Roughness" || definition.Name == "Metallic") && definition.Type == ShaderPropertyType::Float)
                    return true;
                return definition.Name == "AlbedoTexture" || definition.Name == "NormalTexture";
            }

            bool HasShaderProperty(const std::vector<ShaderPropertyDefinition>& definitions, const std::string& name, ShaderPropertyType type)
            {
                return std::any_of(definitions.begin(), definitions.end(),
                    [&](const ShaderPropertyDefinition& definition)
                    {
                        return definition.Name == name && definition.Type == type;
                    });
            }

            ShaderPropertyValue& EnsureSurfaceBackedShaderProperty(MaterialAsset& material, const ShaderPropertyDefinition& definition)
            {
                const auto existing = material.ShaderProperties.find(definition.Name);
                const bool hadSavedValue = existing != material.ShaderProperties.end() && existing->second.Type != ShaderPropertyType::Unknown;
                ShaderPropertyValue& value = material.EnsureShaderPropertyValue(definition);

                // Surface에 이미 있는 값은 인스펙터에서 한 번만 보여준다.
                // 내부 ShaderProperty에도 같은 값을 넣어 두어 커스텀 HLSL이 같은 버퍼를 읽어도 결과가 갈라지지 않는다.
                if (definition.Name == "AlbedoColor" && definition.Type == ShaderPropertyType::Color)
                {
                    if (hadSavedValue)
                        material.AlbedoColor = value.Color;
                    else
                        value.Color = material.AlbedoColor;
                }
                else if (definition.Name == "Roughness" && definition.Type == ShaderPropertyType::Float)
                {
                    if (hadSavedValue)
                        material.Roughness = std::clamp(value.FloatValue, 0.0f, 1.0f);
                    else
                        value.FloatValue = material.Roughness;
                }
                else if (definition.Name == "Metallic" && definition.Type == ShaderPropertyType::Float)
                {
                    if (hadSavedValue)
                        material.Metallic = std::clamp(value.FloatValue, 0.0f, 1.0f);
                    else
                        value.FloatValue = material.Metallic;
                }
                else if (definition.Name == "AlbedoTexture" && definition.Type == ShaderPropertyType::Texture2D)
                {
                    if (hadSavedValue)
                    {
                        material.AlbedoTextureGuid = value.TextureGuid;
                        material.AlbedoTexturePath = value.TexturePath;
                    }
                    else
                    {
                        value.TextureGuid = material.AlbedoTextureGuid;
                        value.TexturePath = material.AlbedoTexturePath;
                    }
                }
                else if (definition.Name == "NormalTexture" && definition.Type == ShaderPropertyType::Texture2D)
                {
                    if (hadSavedValue)
                    {
                        material.NormalTextureGuid = value.TextureGuid;
                        material.NormalTexturePath = value.TexturePath;
                    }
                    else
                    {
                        value.TextureGuid = material.NormalTextureGuid;
                        value.TexturePath = material.NormalTexturePath;
                    }
                }

                return value;
            }

            void SyncSurfaceValueToShaderProperty(MaterialAsset& material, const std::string& name)
            {
                auto it = material.ShaderProperties.find(name);
                if (it == material.ShaderProperties.end())
                    return;

                // 렌더러는 같은 이름의 ShaderProperty를 우선 읽는다.
                // 그래서 Surface 값을 바꿀 때 이 값을 같이 바꾸지 않으면 화면에서는 아래 프로퍼티만 먹는 것처럼 보인다.
                if (name == "AlbedoColor" && it->second.Type == ShaderPropertyType::Color)
                    it->second.Color = material.AlbedoColor;
                else if (name == "Roughness" && it->second.Type == ShaderPropertyType::Float)
                    it->second.FloatValue = material.Roughness;
                else if (name == "Metallic" && it->second.Type == ShaderPropertyType::Float)
                    it->second.FloatValue = material.Metallic;
                else if (name == "AlbedoTexture" && it->second.Type == ShaderPropertyType::Texture2D)
                {
                    it->second.TextureGuid = material.AlbedoTextureGuid;
                    it->second.TexturePath = material.AlbedoTexturePath;
                }
                else if (name == "NormalTexture" && it->second.Type == ShaderPropertyType::Texture2D)
                {
                    it->second.TextureGuid = material.NormalTextureGuid;
                    it->second.TexturePath = material.NormalTexturePath;
                }
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

        void InspectorPanel::ShutdownSharedCaches()
        {
            std::lock_guard<std::mutex> lock(s_MaterialPreviewDebugDumpMutex);
            std::unordered_set<std::string>().swap(s_MaterialPreviewDebugDumpedLabels);
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
            if (m_SelectedAssetType != "material")
            {
                m_SelectedMaterial = MaterialAsset{};
                m_MaterialSavePending = false;
                m_MaterialSaveCountdown = 0.0f;
                m_HasMaterialUndoBaseline = false;
            }
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
            m_HasMaterialUndoBaseline = false;
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
                else if (m_SelectedAssetType == "shader" || m_SelectedAssetType == "visualshader")
                    BuildShaderInspector();
                else
                    BuildGenericAssetInspector();
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

            if (!m_HasMaterialUndoBaseline)
                ResetMaterialUndoBaseline();

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

            std::string shaderLabel = m_SelectedMaterial.ShaderPath.empty()
                ? "Shader: Built-in/" + m_SelectedMaterial.ShaderName
                : "Shader: " + std::filesystem::path(m_SelectedMaterial.ShaderPath).filename().string();
            auto shaderButton = new UI::Button("MaterialShaderButton", shaderLabel);
            shaderButton->SetOnClick([this, shaderButton]()
                {
                    std::string filepath = PlatformUtils::OpenFile("HLSL Shader (*.hlsl)\0*.hlsl\0Legacy CCEngine Shader (*.ccshader)\0*.ccshader\0");
                    if (filepath.empty())
                        return;

                    if (AssetDatabase::GetAssetKind(filepath) != AssetKind::Shader)
                    {
                        ConsoleLog::Warning("Selected file is not a shader asset: " + filepath);
                        return;
                    }

                    // Material은 셰이더 파일을 경로만으로 묶지 않는다.
                    // GUID를 같이 저장해야 파일명을 바꾸거나 폴더를 옮겨도 같은 셰이더를 복구할 수 있다.
                    m_SelectedMaterial.ShaderPath = filepath;
                    m_SelectedMaterial.ShaderGuid = AssetDatabase::GetGuidFromPath(filepath);
                    m_SelectedMaterial.ShaderName = std::filesystem::path(filepath).stem().string();
                    shaderButton->SetText("Shader: " + std::filesystem::path(filepath).filename().string());
                    MarkSelectedMaterialDirty();
                    m_NeedsRebuild = true;
                });
            infoItem->AddChild(shaderButton);

            auto pbrShaderButton = new UI::Button("MaterialUsePBRShaderButton", "Use Base3D PBR Shader");
            pbrShaderButton->SetOnClick([this, shaderButton]()
                {
                    std::filesystem::path shaderPath = std::filesystem::current_path() / "assets" / "shaders" / "Base3D_PBR.hlsl";
                    if (!std::filesystem::exists(shaderPath))
                    {
                        ConsoleLog::Error("Base3D PBR shader is missing: " + shaderPath.string());
                        return;
                    }

                    // 기본 Base3D는 호환용 Lit로 유지하고, PBR은 Material이 명시적으로 선택한다.
                    // 이렇게 해야 새 렌더 기능을 추가해도 기존 씬의 기본 색감이 갑자기 바뀌지 않는다.
                    m_SelectedMaterial.ShaderPath = shaderPath.string();
                    m_SelectedMaterial.ShaderGuid = AssetDatabase::GetGuidFromPath(shaderPath);
                    m_SelectedMaterial.ShaderName = shaderPath.stem().string();
                    shaderButton->SetText("Shader: " + shaderPath.filename().string());
                    MarkSelectedMaterialDirty();
                    m_NeedsRebuild = true;
                });
            infoItem->AddChild(pbrShaderButton);

            auto clearShaderButton = new UI::Button("MaterialClearShaderButton", "Use Built-in Base3D");
            clearShaderButton->SetOnClick([this, shaderButton]()
                {
                    m_SelectedMaterial.ShaderGuid.clear();
                    m_SelectedMaterial.ShaderPath.clear();
                    m_SelectedMaterial.ShaderName = "Base3D";
                    shaderButton->SetText("Shader: Built-in/Base3D");
                    MarkSelectedMaterialDirty();
                    m_NeedsRebuild = true;
                });
            infoItem->AddChild(clearShaderButton);

            auto materialShaderStatusButton = new UI::Button("MaterialShaderStatusText",
                m_SelectedMaterial.ShaderPath.empty()
                ? "Shader Status: Built-in Base3D"
                : BuildShaderStatusText(m_SelectedMaterial.ShaderPath));
            if (m_SelectedMaterial.ShaderPath.empty())
            {
                materialShaderStatusButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                materialShaderStatusButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            }
            else
            {
                ApplyShaderStatusButtonStyle(materialShaderStatusButton, m_SelectedMaterial.ShaderPath);
            }
            infoItem->AddChild(materialShaderStatusButton);

            std::vector<ShaderPropertyDefinition> shaderProperties;
            const bool hasCustomShader = !m_SelectedMaterial.ShaderPath.empty();
            if (!m_SelectedMaterial.ShaderPath.empty())
            {
                shaderProperties = ShaderPropertyParser::LoadFromShaderFile(m_SelectedMaterial.ShaderPath);
                for (const ShaderPropertyDefinition& definition : shaderProperties)
                {
                    if (IsSurfaceBackedShaderProperty(definition))
                        EnsureSurfaceBackedShaderProperty(m_SelectedMaterial, definition);
                }
            }

            auto surfaceItem = new UI::InspectorItem("MaterialSurfaceItem", "Surface");
            surfaceItem->SetAnchorMin(0.0f, 0.0f);
            surfaceItem->SetAnchorMax(1.0f, 0.0f);
            AddChild(surfaceItem);

            // Material 에셋을 직접 편집한다. MeshComponent 값과 섞지 않아야 같은 재질을 쓰는 오브젝트들이 한 기준을 공유한다.
            const bool showAlbedoColor = !hasCustomShader || HasShaderProperty(shaderProperties, "AlbedoColor", ShaderPropertyType::Color);
            const bool showAlbedoTexture = !hasCustomShader || HasShaderProperty(shaderProperties, "AlbedoTexture", ShaderPropertyType::Texture2D);
            const bool showNormalTexture = HasShaderProperty(shaderProperties, "NormalTexture", ShaderPropertyType::Texture2D);
            const bool showRoughness = !hasCustomShader || HasShaderProperty(shaderProperties, "Roughness", ShaderPropertyType::Float);
            const bool showMetallic = !hasCustomShader || HasShaderProperty(shaderProperties, "Metallic", ShaderPropertyType::Float);

            if (showAlbedoColor)
            {
                InspectorUtils::AddColor4(surfaceItem, "MaterialAssetAlbedo", "Albedo",
                    [this]() { return m_SelectedMaterial.AlbedoColor; },
                    [this](DirectX::XMFLOAT4 value)
                    {
                        m_SelectedMaterial.AlbedoColor = value;
                        SyncSurfaceValueToShaderProperty(m_SelectedMaterial, "AlbedoColor");
                        MarkSelectedMaterialDirty();
                    });
            }

            if (showAlbedoTexture)
            {
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
                        SyncSurfaceValueToShaderProperty(m_SelectedMaterial, "AlbedoTexture");
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
                        SyncSurfaceValueToShaderProperty(m_SelectedMaterial, "AlbedoTexture");
                        albedoButton->SetText("Albedo Texture: (none)");
                        MarkSelectedMaterialDirty();
                    });
                surfaceItem->AddChild(clearAlbedoButton);
            }

            if (showNormalTexture)
            {
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
                        SyncSurfaceValueToShaderProperty(m_SelectedMaterial, "NormalTexture");
                        normalButton->SetText("Normal Texture: " + std::filesystem::path(filepath).filename().string());
                        MarkSelectedMaterialDirty();
                    });
                surfaceItem->AddChild(normalButton);
            }

            if (showRoughness)
            {
                InspectorUtils::AddDragFloat(surfaceItem, "MaterialAssetRoughness", "Roughness",
                    [this]() { return m_SelectedMaterial.Roughness; },
                    [this](float value)
                    {
                        m_SelectedMaterial.Roughness = std::clamp(value, 0.0f, 1.0f);
                        SyncSurfaceValueToShaderProperty(m_SelectedMaterial, "Roughness");
                        MarkSelectedMaterialDirty();
                    });
            }

            if (showMetallic)
            {
                InspectorUtils::AddDragFloat(surfaceItem, "MaterialAssetMetallic", "Metallic",
                    [this]() { return m_SelectedMaterial.Metallic; },
                    [this](float value)
                    {
                        m_SelectedMaterial.Metallic = std::clamp(value, 0.0f, 1.0f);
                        SyncSurfaceValueToShaderProperty(m_SelectedMaterial, "Metallic");
                        MarkSelectedMaterialDirty();
                    });
            }

            if (!shaderProperties.empty())
            {
                const bool hasVisibleShaderProperties = std::any_of(shaderProperties.begin(), shaderProperties.end(),
                    [](const ShaderPropertyDefinition& definition)
                    {
                        return !IsSurfaceBackedShaderProperty(definition);
                    });

                if (hasVisibleShaderProperties)
                {
                    auto shaderPropertyItem = new UI::InspectorItem("MaterialShaderPropertiesItem", "Shader Properties");
                    shaderPropertyItem->SetAnchorMin(0.0f, 0.0f);
                    shaderPropertyItem->SetAnchorMax(1.0f, 0.0f);
                    AddChild(shaderPropertyItem);

                    for (const ShaderPropertyDefinition& definition : shaderProperties)
                    {
                        ShaderPropertyValue& value = m_SelectedMaterial.EnsureShaderPropertyValue(definition);
                        if (IsSurfaceBackedShaderProperty(definition))
                            continue;

                        const std::string widgetName = "ShaderProperty_" + definition.Name;

                        if (definition.Type == ShaderPropertyType::Color)
                        {
                            InspectorUtils::AddColor4(shaderPropertyItem, widgetName, definition.DisplayName,
                                [this, propertyName = definition.Name]() { return m_SelectedMaterial.ShaderProperties[propertyName].Color; },
                                [this, propertyName = definition.Name](DirectX::XMFLOAT4 newValue)
                                {
                                    m_SelectedMaterial.ShaderProperties[propertyName].Color = newValue;
                                    if (propertyName == "AlbedoColor")
                                        m_SelectedMaterial.AlbedoColor = newValue;
                                    MarkSelectedMaterialDirty();
                                });
                        }
                        else if (definition.Type == ShaderPropertyType::Float)
                        {
                            InspectorUtils::AddDragFloat(shaderPropertyItem, widgetName, definition.DisplayName,
                                [this, propertyName = definition.Name]() { return m_SelectedMaterial.ShaderProperties[propertyName].FloatValue; },
                                [this, propertyName = definition.Name, definition](float newValue)
                                {
                                    if (definition.HasRange)
                                        newValue = std::clamp(newValue, definition.Min, definition.Max);
                                    m_SelectedMaterial.ShaderProperties[propertyName].FloatValue = newValue;
                                    if (propertyName == "Roughness")
                                        m_SelectedMaterial.Roughness = std::clamp(newValue, 0.0f, 1.0f);
                                    else if (propertyName == "Metallic")
                                        m_SelectedMaterial.Metallic = std::clamp(newValue, 0.0f, 1.0f);
                                    MarkSelectedMaterialDirty();
                                });
                        }
                        else if (definition.Type == ShaderPropertyType::Toggle)
                        {
                            auto toggleButton = new UI::Button(widgetName, definition.DisplayName + ": " + (value.BoolValue ? "On" : "Off"));
                            toggleButton->SetOnClick([this, toggleButton, propertyName = definition.Name, label = definition.DisplayName]()
                                {
                                    ShaderPropertyValue& toggleValue = m_SelectedMaterial.ShaderProperties[propertyName];
                                    toggleValue.BoolValue = !toggleValue.BoolValue;
                                    toggleButton->SetText(label + ": " + (toggleValue.BoolValue ? "On" : "Off"));
                                    MarkSelectedMaterialDirty();
                                });
                            shaderPropertyItem->AddChild(toggleButton);
                        }
                        else if (definition.Type == ShaderPropertyType::Texture2D)
                        {
                            auto textureButton = new UI::Button(widgetName,
                                value.TexturePath.empty()
                                ? definition.DisplayName + ": (none)"
                                : definition.DisplayName + ": " + std::filesystem::path(value.TexturePath).filename().string());
                            textureButton->SetOnClick([this, textureButton, propertyName = definition.Name, label = definition.DisplayName]()
                                {
                                    std::string filepath = PlatformUtils::OpenFile("Texture (*.png;*.jpg;*.jpeg;*.tga)\0*.png;*.jpg;*.jpeg;*.tga\0");
                                    if (filepath.empty())
                                        return;

                                    ShaderPropertyValue& textureValue = m_SelectedMaterial.ShaderProperties[propertyName];
                                    textureValue.TexturePath = filepath;
                                    textureValue.TextureGuid = AssetDatabase::GetGuidFromPath(filepath);
                                    if (propertyName == "AlbedoTexture")
                                    {
                                        m_SelectedMaterial.AlbedoTexturePath = filepath;
                                        m_SelectedMaterial.AlbedoTextureGuid = textureValue.TextureGuid;
                                        m_SelectedMaterial.AlbedoTexture.reset(Texture2D::Create(filepath));
                                    }
                                    textureButton->SetText(label + ": " + std::filesystem::path(filepath).filename().string());
                                    MarkSelectedMaterialDirty();
                                });
                            shaderPropertyItem->AddChild(textureButton);
                        }
                    }
                }
            }

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

        void InspectorPanel::BuildShaderInspector()
        {
            if (m_SelectedAssetPath.extension() == ".ccvshader")
            {
                VisualShaderAsset graph;
                if (!graph.LoadFromFile(m_SelectedAssetPath))
                    graph = VisualShaderAsset::CreateDefault(m_SelectedAssetPath.stem().string());

                auto infoItem = new UI::InspectorItem("VisualShaderInfoItem", "Material Graph");
                infoItem->SetAnchorMin(0.0f, 0.0f);
                infoItem->SetAnchorMax(1.0f, 0.0f);
                AddChild(infoItem);

                auto nameButton = new UI::Button("VisualShaderNameText", "Name: " + graph.Name);
                nameButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                nameButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                infoItem->AddChild(nameButton);

                auto pathButton = new UI::Button("VisualShaderPathText", "File: " + m_SelectedAssetPath.filename().string());
                pathButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                pathButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                infoItem->AddChild(pathButton);

                std::filesystem::path generatedPath = VisualShaderAsset::GetGeneratedHlslPath(m_SelectedAssetPath);
                auto generatedButton = new UI::Button("VisualShaderGeneratedText", "Generated: " + generatedPath.filename().string());
                generatedButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                generatedButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                infoItem->AddChild(generatedButton);

                auto openGraphButton = new UI::Button("VisualShaderOpenGraphButton", "Open Material Graph");
                openGraphButton->SetOnClick([this]()
                    {
                        if (m_OnOpenShaderEditor)
                            m_OnOpenShaderEditor(m_SelectedAssetPath);
                    });
                infoItem->AddChild(openGraphButton);

                auto generateButton = new UI::Button("VisualShaderGenerateButton", "Generate HLSL");
                generateButton->SetOnClick([this]()
                    {
                        VisualShaderAsset currentGraph;
                        if (!currentGraph.LoadFromFile(m_SelectedAssetPath))
                        {
                            ConsoleLog::Error("Visual shader graph load failed: " + m_SelectedAssetPath.string());
                            return;
                        }

                        // 그래프 에셋은 편집 데이터이고 generated.hlsl은 컴파일 입력이다.
                        // 사용자가 명시적으로 Generate를 누르면 두 파일의 관계를 즉시 다시 맞춘다.
                        if (currentGraph.SaveGeneratedHlsl(m_SelectedAssetPath))
                        {
                            AssetDatabase::EnsureMetaFile(VisualShaderAsset::GetGeneratedHlslPath(m_SelectedAssetPath));
                            ConsoleLog::Info("Visual shader HLSL generated: " + VisualShaderAsset::GetGeneratedHlslPath(m_SelectedAssetPath).string());
                            if (m_OnAssetChanged)
                                m_OnAssetChanged(m_SelectedAssetPath, m_SelectedAssetType);
                        }
                    });
                infoItem->AddChild(generateButton);

                auto openGeneratedButton = new UI::Button("VisualShaderOpenGeneratedButton", "Open Generated HLSL");
                openGeneratedButton->SetOnClick([this, generatedPath]()
                    {
                        if (m_OnOpenShaderEditor)
                            m_OnOpenShaderEditor(generatedPath);
                    });
                infoItem->AddChild(openGeneratedButton);

                auto& window = CCEngine::Application::Get()->GetWindow();
                UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
                return;
            }

            ShaderAsset shader;
            if (!shader.LoadFromFile(m_SelectedAssetPath))
            {
                auto errorItem = new UI::InspectorItem("ShaderLoadError", "Shader");
                errorItem->SetAnchorMin(0.0f, 0.0f);
                errorItem->SetAnchorMax(1.0f, 0.0f);
                auto errorButton = new UI::Button("ShaderLoadErrorText", "Failed to load shader.");
                errorButton->SetNormalColor({ 0.20f, 0.08f, 0.08f, 1.0f });
                errorButton->SetHoverColor({ 0.20f, 0.08f, 0.08f, 1.0f });
                errorItem->AddChild(errorButton);
                AddChild(errorItem);
                return;
            }

            auto infoItem = new UI::InspectorItem("ShaderInfoItem", "Shader Asset");
            infoItem->SetAnchorMin(0.0f, 0.0f);
            infoItem->SetAnchorMax(1.0f, 0.0f);
            AddChild(infoItem);

            auto nameButton = new UI::Button("ShaderNameText", "Name: " + shader.Name);
            nameButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            nameButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(nameButton);

            auto pathButton = new UI::Button("ShaderPathText", "File: " + m_SelectedAssetPath.filename().string());
            pathButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            pathButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(pathButton);

            auto templateButton = new UI::Button("ShaderTemplateText", "Template: " + shader.TemplateName);
            templateButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            templateButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(templateButton);

            auto entryButton = new UI::Button("ShaderEntryText",
                "Entries: " + shader.VertexEntry + " / " + shader.PixelEntry);
            entryButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            entryButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(entryButton);

            const bool isHlsl = ShaderCompiler::IsHlslSource(m_SelectedAssetPath);
            auto compileStateButton = new UI::Button("ShaderCompileStateText",
                isHlsl ? BuildShaderStatusText(m_SelectedAssetPath) : "Shader Status: legacy shader metadata");
            ApplyShaderStatusButtonStyle(compileStateButton, m_SelectedAssetPath);
            infoItem->AddChild(compileStateButton);

            auto compileButton = new UI::Button("ShaderCompileButton", "Compile Shader");
            compileButton->SetNormalColor({ 0.18f, 0.18f, 0.20f, 1.0f });
            compileButton->SetHoverColor({ 0.24f, 0.24f, 0.27f, 1.0f });
            compileButton->SetOnClick([this, compileStateButton]()
                {
                    if (!ShaderCompiler::IsHlslSource(m_SelectedAssetPath))
                    {
                        ConsoleLog::Warning("Shader compile skipped. Select an .hlsl shader source.");
                        return;
                    }

                    RuntimeShaderLibrary::Invalidate(m_SelectedAssetPath);
                    ShaderCompileResult result = ShaderCompiler::CompileHlslFile(m_SelectedAssetPath, true);
                    if (result.Success)
                    {
                        compileStateButton->SetText(BuildShaderStatusText(m_SelectedAssetPath));
                        ApplyShaderStatusButtonStyle(compileStateButton, m_SelectedAssetPath);
                        ConsoleLog::Info(result.Summary);
                    }
                    else
                    {
                        compileStateButton->SetText("Shader Status: compile failed");
                        compileStateButton->SetNormalColor({ 0.34f, 0.08f, 0.12f, 1.0f });
                        compileStateButton->SetHoverColor({ 0.42f, 0.10f, 0.15f, 1.0f });
                        ConsoleLog::Error(result.Summary);
                    }
                });
            infoItem->AddChild(compileButton);

            auto openEditorButton = new UI::Button("ShaderOpenEditorButton", "Open in Visual Studio");
            openEditorButton->SetNormalColor({ 0.18f, 0.18f, 0.20f, 1.0f });
            openEditorButton->SetHoverColor({ 0.24f, 0.24f, 0.27f, 1.0f });
            openEditorButton->SetOnClick([this]()
                {
                    if (m_OnOpenShaderEditor)
                        m_OnOpenShaderEditor(m_SelectedAssetPath);
                });
            infoItem->AddChild(openEditorButton);

            if (isHlsl)
            {
                m_SelectedMaterial = BuildShaderPreviewMaterial(m_SelectedAssetPath);
                m_MaterialPreviewDirty = true;

                auto previewLabel = new UI::Button("ShaderPreviewLabel", "Preview");
                previewLabel->SetNormalColor({ 0.12f, 0.12f, 0.13f, 1.0f });
                previewLabel->SetHoverColor({ 0.12f, 0.12f, 0.13f, 1.0f });
                AddChild(previewLabel);

                EnsureMaterialPreviewResources();
                m_MaterialPreviewImage = new UI::ImageWidget("ShaderPreviewImage",
                    m_MaterialPreviewFramebuffer ? m_MaterialPreviewFramebuffer->GetColorAttachmentRendererID(0) : nullptr);
                m_MaterialPreviewImage->SetAnchorMin(0.0f, 0.0f);
                m_MaterialPreviewImage->SetAnchorMax(1.0f, 0.0f);
                AddChild(m_MaterialPreviewImage);
            }

            auto& window = CCEngine::Application::Get()->GetWindow();
            UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

        MaterialAsset InspectorPanel::BuildShaderPreviewMaterial(const std::filesystem::path& shaderPath) const
        {
            MaterialAsset preview = MaterialAsset::CreateDefault(shaderPath.stem().string() + " Preview");
            preview.ShaderName = shaderPath.stem().string();
            preview.ShaderPath = shaderPath.string();
            preview.ShaderGuid = AssetDatabase::GetGuidFromPath(shaderPath);
            preview.AlbedoColor = { 0.82f, 0.82f, 0.88f, 1.0f };

            // Shader Preview는 파일을 저장하지 않는 임시 Material을 사용한다.
            // 이 Material이 실제 Material과 같은 Property/Runtime Shader 경로를 타야, 미리보기와 실제 렌더 결과가 어긋나지 않는다.
            for (const ShaderPropertyDefinition& definition : ShaderPropertyParser::LoadFromShaderFile(shaderPath))
            {
                ShaderPropertyValue& value = preview.EnsureShaderPropertyValue(definition);
                if (definition.Name == "AlbedoColor" && definition.Type == ShaderPropertyType::Color)
                    preview.AlbedoColor = value.Color;
                else if (definition.Name == "Roughness" && definition.Type == ShaderPropertyType::Float)
                    preview.Roughness = value.FloatValue;
                else if (definition.Name == "Metallic" && definition.Type == ShaderPropertyType::Float)
                    preview.Metallic = value.FloatValue;
                // 셰이더 파일은 어떤 입력이 필요한지만 정의한다.
                // 실제 텍스처 선택은 Material 인스펙터에서만 다루어야 에셋과 렌더 상태가 섞이지 않는다.
            }

            return preview;
        }

        void InspectorPanel::BuildGenericAssetInspector()
        {
            m_SelectedMaterial = MaterialAsset{};
            m_MaterialSavePending = false;
            m_MaterialSaveCountdown = 0.0f;
            m_MaterialPreviewDirty = true;

            auto infoItem = new UI::InspectorItem("GenericAssetInfoItem", "Asset");
            infoItem->SetAnchorMin(0.0f, 0.0f);
            infoItem->SetAnchorMax(1.0f, 0.0f);
            AddChild(infoItem);

            const std::string fileName = m_SelectedAssetPath.filename().string();
            const std::string extension = m_SelectedAssetPath.extension().string();

            auto nameButton = new UI::Button("GenericAssetNameText", "Name: " + fileName);
            nameButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            nameButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(nameButton);

            auto typeButton = new UI::Button("GenericAssetTypeText", "Type: " + (m_SelectedAssetType.empty() ? "unknown" : m_SelectedAssetType));
            typeButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            typeButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(typeButton);

            auto extensionButton = new UI::Button("GenericAssetExtensionText", "Extension: " + (extension.empty() ? "(none)" : extension));
            extensionButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            extensionButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(extensionButton);

            std::error_code ec;
            uintmax_t fileSize = std::filesystem::is_regular_file(m_SelectedAssetPath, ec)
                ? std::filesystem::file_size(m_SelectedAssetPath, ec)
                : 0;
            auto sizeButton = new UI::Button("GenericAssetSizeText", "Size: " + std::to_string(fileSize) + " bytes");
            sizeButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            sizeButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(sizeButton);

            auto pathButton = new UI::Button("GenericAssetPathText", "Path: " + m_SelectedAssetPath.string());
            pathButton->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            pathButton->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
            infoItem->AddChild(pathButton);

            if (m_SelectedAssetType == "script" || m_SelectedAssetType == "shader")
            {
                auto openButton = new UI::Button("GenericAssetOpenButton", "Open in External Editor");
                openButton->SetOnClick([this]()
                    {
                        if (m_OnOpenShaderEditor)
                            m_OnOpenShaderEditor(m_SelectedAssetPath);
                    });
                infoItem->AddChild(openButton);
            }

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
            if ((m_SelectedAssetType != "material" && m_SelectedAssetType != "shader") || m_SelectedAssetPath.empty())
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

            if (m_HasMaterialUndoBaseline && !SameMaterialForUndo(m_MaterialUndoBaseline, m_SelectedMaterial))
            {
                MaterialUndoRecord record;
                record.Path = m_SelectedAssetPath;
                record.Label = "Edit Material " + m_SelectedAssetPath.filename().string();
                record.Before = m_MaterialUndoBaseline;
                record.After = m_SelectedMaterial;
                m_MaterialUndoStack.push_back(record);
                if (m_MaterialUndoStack.size() > MaxMaterialUndoRecords)
                    m_MaterialUndoStack.erase(m_MaterialUndoStack.begin());
                m_MaterialRedoStack.clear();
            }

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

            ResetMaterialUndoBaseline();
        }

        bool InspectorPanel::UndoMaterialEdit()
        {
            if (m_SelectedAssetPath.empty() || m_SelectedAssetType != "material")
                return false;

            FlushSelectedMaterialSave();
            if (m_MaterialUndoStack.empty())
                return false;

            MaterialUndoRecord record = m_MaterialUndoStack.back();
            m_MaterialUndoStack.pop_back();
            m_MaterialRedoStack.push_back(record);

            m_SelectedMaterial = record.Before;
            m_SelectedMaterial.SaveToFile(record.Path);
            m_MaterialPreviewDirty = true;
            ResetMaterialUndoBaseline();
            if (m_OnMaterialPreviewChanged)
                m_OnMaterialPreviewChanged(record.Path, m_SelectedMaterial);
            if (m_OnAssetChanged)
                m_OnAssetChanged(record.Path, "material");
            m_NeedsRebuild = true;
            ConsoleLog::Info("Undo material edit: " + record.Path.filename().string());
            return true;
        }

        bool InspectorPanel::RedoMaterialEdit()
        {
            if (m_SelectedAssetPath.empty() || m_SelectedAssetType != "material")
                return false;

            FlushSelectedMaterialSave();
            if (m_MaterialRedoStack.empty())
                return false;

            MaterialUndoRecord record = m_MaterialRedoStack.back();
            m_MaterialRedoStack.pop_back();
            m_MaterialUndoStack.push_back(record);

            m_SelectedMaterial = record.After;
            m_SelectedMaterial.SaveToFile(record.Path);
            m_MaterialPreviewDirty = true;
            ResetMaterialUndoBaseline();
            if (m_OnMaterialPreviewChanged)
                m_OnMaterialPreviewChanged(record.Path, m_SelectedMaterial);
            if (m_OnAssetChanged)
                m_OnAssetChanged(record.Path, "material");
            m_NeedsRebuild = true;
            ConsoleLog::Info("Redo material edit: " + record.Path.filename().string());
            return true;
        }

        void InspectorPanel::ResetMaterialUndoBaseline()
        {
            m_MaterialUndoBaseline = m_SelectedMaterial;
            m_HasMaterialUndoBaseline = (m_SelectedAssetType == "material" && !m_SelectedAssetPath.empty());
        }

        bool InspectorPanel::SameMaterialForUndo(const MaterialAsset& a, const MaterialAsset& b) const
        {
            auto sameFloat4 = [](const DirectX::XMFLOAT4& left, const DirectX::XMFLOAT4& right)
            {
                return left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w;
            };

            if (a.Name != b.Name || a.ShaderName != b.ShaderName || a.ShaderGuid != b.ShaderGuid || a.ShaderPath != b.ShaderPath)
                return false;
            if (!sameFloat4(a.AlbedoColor, b.AlbedoColor) || a.Roughness != b.Roughness || a.Metallic != b.Metallic)
                return false;
            if (a.AlbedoTextureGuid != b.AlbedoTextureGuid || a.AlbedoTexturePath != b.AlbedoTexturePath ||
                a.NormalTextureGuid != b.NormalTextureGuid || a.NormalTexturePath != b.NormalTexturePath)
                return false;
            if (a.ShaderProperties.size() != b.ShaderProperties.size())
                return false;

            for (const auto& [name, left] : a.ShaderProperties)
            {
                auto it = b.ShaderProperties.find(name);
                if (it == b.ShaderProperties.end())
                    return false;
                const auto& right = it->second;
                if (left.Type != right.Type || !sameFloat4(left.Color, right.Color) ||
                    left.FloatValue != right.FloatValue || left.BoolValue != right.BoolValue ||
                    left.TextureGuid != right.TextureGuid || left.TexturePath != right.TexturePath)
                    return false;
            }

            return true;
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

            if (e.GetEventType() == EventType::KeyPressed && m_SelectedAssetType == "material" && Widget::IsKeyboardFocusOwner(this))
            {
                auto& ke = static_cast<KeyPressedEvent&>(e);
                const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                if (ctrl && ke.GetKeyCode() == 'Z')
                {
                    const bool handled = shift ? RedoMaterialEdit() : UndoMaterialEdit();
                    e.Handled = handled;
                    return handled;
                }
                if (ctrl && ke.GetKeyCode() == 'Y')
                {
                    const bool handled = RedoMaterialEdit();
                    e.Handled = handled;
                    return handled;
                }
            }

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

                if (child->GetName() == "MaterialPreviewLabel" || child->GetName() == "ShaderPreviewLabel")
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
