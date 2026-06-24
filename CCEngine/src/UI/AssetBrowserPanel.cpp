#include "UI/AssetBrowserPanel.h"
#include "Core/AssetDatabase.h"
#include "Renderer/UIRenderer.h"
#include "Application.h"
#include "Core/Window.h"
#include "Events/KeyEvent.h"
#include <algorithm>
#include <iostream>

namespace CCEngine
{
    namespace UI
    {
        AssetBrowserPanel::AssetBrowserPanel(const std::string& name)
            : WindowPanel(name, "Asset Browser")
        {
            SetClipToBounds(true);
            SetRootDirectory(std::filesystem::current_path() / "assets");
        }

        void AssetBrowserPanel::SetRootDirectory(const std::filesystem::path& rootDirectory)
        {
            m_RootDirectory = rootDirectory;
            m_CurrentDirectory = rootDirectory;
            Refresh();
        }

        void AssetBrowserPanel::Refresh()
        {
            // 에셋 브라우저 새로고침은 프로젝트 파일을 확인하는 시점이다.
            // 여기서 meta를 맞춰 두면 저장 시스템은 GUID를 안정적으로 사용할 수 있다.
            AssetDatabase::Scan(m_RootDirectory);

            m_Entries.clear();
            m_SelectedIndex = -1;
            m_HoveredIndex = -1;
            m_LastClickedIndex = -1;
            m_ContextMenuVisible = false;

            if (!std::filesystem::exists(m_CurrentDirectory))
                return;

            if (m_CurrentDirectory != m_RootDirectory)
            {
                AssetEntry parentEntry;
                parentEntry.Path = m_CurrentDirectory.parent_path();
                parentEntry.DisplayName = "..";
                parentEntry.Type = AssetType::Folder;
                m_Entries.push_back(parentEntry);
            }

            for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
            {
                AssetType type = AssetType::Unknown;
                if (entry.is_directory())
                {
                    type = AssetType::Folder;
                }
                else if (entry.is_regular_file())
                {
                    type = GetAssetType(entry.path());
                }

                if (type == AssetType::Unknown)
                    continue;

                AssetEntry assetEntry;
                assetEntry.Path = entry.path();
                assetEntry.Type = type;
                assetEntry.DisplayName = entry.path().filename().string();

                m_Entries.push_back(assetEntry);
            }

            std::sort(m_Entries.begin(), m_Entries.end(), [](const AssetEntry& a, const AssetEntry& b)
                {
                    if (a.DisplayName == "..") return true;
                    if (b.DisplayName == "..") return false;
                    if (a.Type != b.Type)
                        return static_cast<int>(a.Type) < static_cast<int>(b.Type);
                    return a.DisplayName < b.DisplayName;
                });
        }

        AssetBrowserPanel::AssetType AssetBrowserPanel::GetAssetType(const std::filesystem::path& path) const
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            if (extension == ".ccscene") return AssetType::Scene;
            if (extension == ".ccprefab") return AssetType::Prefab;
            if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb") return AssetType::Model;
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga") return AssetType::Texture;
            return AssetType::Unknown;
        }

        std::string AssetBrowserPanel::GetTypeLabel(AssetType type) const
        {
            switch (type)
            {
                case AssetType::Folder: return "DIR";
                case AssetType::Scene: return "SCN";
                case AssetType::Prefab: return "PFB";
                case AssetType::Model: return "MDL";
                case AssetType::Texture: return "TEX";
                default: return "???";
            }
        }

        std::string AssetBrowserPanel::GetTypeKey(AssetType type) const
        {
            switch (type)
            {
                case AssetType::Scene: return "scene";
                case AssetType::Prefab: return "prefab";
                case AssetType::Model: return "model";
                case AssetType::Texture: return "texture";
                case AssetType::Folder: return "folder";
                default: return "unknown";
            }
        }

        void AssetBrowserPanel::ActivateEntry(const AssetEntry& entry)
        {
            std::string path = entry.Path.string();

            switch (entry.Type)
            {
                case AssetType::Folder:
                    NavigateTo(entry.Path);
                    break;
                case AssetType::Scene:
                    if (m_OnSceneSelected) m_OnSceneSelected(path);
                    break;
                case AssetType::Prefab:
                    if (m_OnPrefabSelected) m_OnPrefabSelected(path);
                    break;
                case AssetType::Model:
                    if (m_OnModelSelected) m_OnModelSelected(path);
                    break;
                default:
                    std::cout << "[AssetBrowser] No action for asset: " << path << std::endl;
                    break;
            }
        }

        bool AssetBrowserPanel::DeleteSelectedAsset()
        {
            if (m_SelectedIndex < 0 || m_SelectedIndex >= (int)m_Entries.size())
                return false;

            const auto& entry = m_Entries[m_SelectedIndex];
            if (entry.Type == AssetType::Folder || entry.Type == AssetType::Unknown)
                return false;

            std::error_code ec;
            auto target = std::filesystem::weakly_canonical(entry.Path, ec);
            if (ec || !std::filesystem::exists(target) || !std::filesystem::is_regular_file(target))
                return false;

            auto root = std::filesystem::weakly_canonical(m_RootDirectory, ec);
            if (ec)
                return false;

            std::wstring targetString = target.wstring();
            std::wstring rootString = root.wstring();
            if (targetString.rfind(rootString, 0) != 0)
                return false;

            // 에셋 루트 내부의 일반 파일만 지운다. 프로젝트 바깥 경로와 폴더 삭제는 여기서 막는다.
            if (!std::filesystem::remove(target, ec) || ec)
                return false;

            // 에셋을 지우면 짝이 되는 meta도 같이 지운다. 둘이 어긋나면 GUID 참조가 남아 버린다.
            AssetDatabase::DeleteMetaFile(target);

            std::cout << "[AssetBrowser] Asset deleted: " << target.string() << std::endl;
            Refresh();
            return true;
        }

        void AssetBrowserPanel::NavigateTo(const std::filesystem::path& directory)
        {
            std::error_code ec;
            auto target = std::filesystem::weakly_canonical(directory, ec);
            if (ec || !std::filesystem::exists(target) || !std::filesystem::is_directory(target))
                return;

            auto root = std::filesystem::weakly_canonical(m_RootDirectory, ec);
            if (ec)
                return;

            std::wstring targetString = target.wstring();
            std::wstring rootString = root.wstring();
            if (targetString.rfind(rootString, 0) != 0)
                return;

            m_CurrentDirectory = target;
            m_ScrollState.ScrollY = 0.0f;
            Refresh();
        }

        bool AssetBrowserPanel::IsContentPoint(float mouseX, float mouseY) const
        {
            return mouseX >= m_CalculatedPos.x &&
                mouseX <= m_CalculatedPos.x + m_CalculatedSize.x - 20.0f &&
                mouseY >= m_CalculatedPos.y + m_ContentTop &&
                mouseY <= m_CalculatedPos.y + m_CalculatedSize.y;
        }

        bool AssetBrowserPanel::IsScrollbarPoint(float mouseX, float mouseY) const
        {
            if (m_ScrollState.GetMaxScroll() <= 0.0f)
                return false;

            float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 16.0f;
            float trackTop = m_CalculatedPos.y + m_ContentTop;
            float trackBottom = trackTop + m_ScrollState.ViewportHeight;

            return mouseX >= thumbX &&
                mouseX <= thumbX + 8.0f &&
                mouseY >= trackTop &&
                mouseY <= trackBottom;
        }

        int AssetBrowserPanel::GetEntryIndexAt(float mouseX, float mouseY) const
        {
            if (!IsContentPoint(mouseX, mouseY))
                return -1;

            float localY = mouseY - (m_CalculatedPos.y + m_ContentTop + 8.0f) + m_ScrollState.ScrollY;
            int index = (int)(localY / (m_RowHeight + m_RowGap));
            if (index < 0 || index >= (int)m_Entries.size())
                return -1;

            return index;
        }

        bool AssetBrowserPanel::IsContextMenuPoint(float mouseX, float mouseY) const
        {
            return m_ContextMenuVisible &&
                mouseX >= m_ContextMenuX &&
                mouseX <= m_ContextMenuX + m_ContextMenuWidth &&
                mouseY >= m_ContextMenuY &&
                mouseY <= m_ContextMenuY + m_ContextMenuHeight;
        }

        void AssetBrowserPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            WindowPanel::UpdateLayout(parentPos, parentSize);

            m_ScrollState.ContentHeight = (float)m_Entries.size() * (m_RowHeight + m_RowGap);
            m_ScrollState.ViewportHeight = (std::max)(0.0f, m_CalculatedSize.y - m_ContentTop);
        }

        void AssetBrowserPanel::OnRender()
        {
            if (!m_IsVisible)
                return;

            SetClipPadding(0.0f, m_ContentTop, 0.0f, 0.0f);
            WindowPanel::OnRender();

            Window* mouseWindow = GetOwnerWindow() ? GetOwnerWindow() : &CCEngine::Application::Get()->GetWindow();
            auto [mouseX, mouseY] = mouseWindow->GetMousePosition();
            m_HoveredIndex = -1;

            std::error_code ec;
            auto relativeCurrent = std::filesystem::relative(m_CurrentDirectory, m_RootDirectory, ec);
            std::string pathLabel = ec || relativeCurrent.empty() ? "assets" : ("assets\\" + relativeCurrent.string());
            UIRenderer::DrawString(pathLabel, m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + 43.0f, { 0.62f, 0.62f, 0.62f, 1.0f });

            float rowX = m_CalculatedPos.x + 8.0f;
            float rowY = m_CalculatedPos.y + m_ContentTop + 8.0f - m_ScrollState.ScrollY;
            float rowWidth = m_CalculatedSize.x - 28.0f;

            for (size_t i = 0; i < m_Entries.size(); ++i)
            {
                float currentY = rowY + (float)i * (m_RowHeight + m_RowGap);
                if (currentY + m_RowHeight < m_CalculatedPos.y + m_ContentTop || currentY > m_CalculatedPos.y + m_CalculatedSize.y)
                    continue;

                bool hovered = !Widget::IsMouseInteractionActive() &&
                    mouseX >= rowX && mouseX <= rowX + rowWidth && mouseY >= currentY && mouseY <= currentY + m_RowHeight;
                if (hovered)
                    m_HoveredIndex = (int)i;

                DirectX::XMFLOAT4 rowColor = { 0.13f, 0.13f, 0.14f, 1.0f };
                if ((int)i == m_SelectedIndex) rowColor = { 0.18f, 0.30f, 0.42f, 1.0f };
                else if (hovered) rowColor = { 0.20f, 0.20f, 0.21f, 1.0f };

                UIRenderer::DrawRectFilled(rowX, currentY, rowWidth, m_RowHeight, rowColor);

                DirectX::XMFLOAT4 badgeColor = { 0.28f, 0.28f, 0.30f, 1.0f };
                if (m_Entries[i].Type == AssetType::Folder) badgeColor = { 0.42f, 0.34f, 0.18f, 1.0f };
                else if (m_Entries[i].Type == AssetType::Prefab) badgeColor = { 0.28f, 0.40f, 0.62f, 1.0f };
                else if (m_Entries[i].Type == AssetType::Model) badgeColor = { 0.34f, 0.48f, 0.32f, 1.0f };
                else if (m_Entries[i].Type == AssetType::Scene) badgeColor = { 0.48f, 0.36f, 0.22f, 1.0f };
                else if (m_Entries[i].Type == AssetType::Texture) badgeColor = { 0.45f, 0.32f, 0.45f, 1.0f };

                UIRenderer::DrawRectFilled(rowX + 5.0f, currentY + 5.0f, 36.0f, 16.0f, badgeColor);
                UIRenderer::DrawString(GetTypeLabel(m_Entries[i].Type), rowX + 10.0f, currentY + 18.0f, { 0.95f, 0.95f, 0.95f, 1.0f });
                UIRenderer::DrawString(m_Entries[i].DisplayName, rowX + 50.0f, currentY + 18.0f, { 0.86f, 0.86f, 0.86f, 1.0f });
            }

            if (m_Entries.empty())
            {
                UIRenderer::DrawString("No supported assets found.", m_CalculatedPos.x + 12.0f, m_CalculatedPos.y + m_ContentTop + 24.0f, { 0.65f, 0.65f, 0.65f, 1.0f });
            }

            if (m_ScrollState.GetMaxScroll() > 0.0f)
            {
                float thumbH = m_ScrollState.GetThumbHeight();
                float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + m_ContentTop);
                float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 16.0f;

                UIRenderer::DrawRect({ thumbX, m_CalculatedPos.y + m_ContentTop }, { 8.0f, m_ScrollState.ViewportHeight }, { 0.08f, 0.08f, 0.08f, 0.5f });
                UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.42f, 0.42f, 0.42f, 1.0f });
            }

            if (m_ContextMenuVisible)
            {
                UIRenderer::DrawRectFilled(m_ContextMenuX, m_ContextMenuY, m_ContextMenuWidth, m_ContextMenuHeight, { 0.14f, 0.14f, 0.15f, 1.0f });
                UIRenderer::DrawRect({ m_ContextMenuX, m_ContextMenuY }, { m_ContextMenuWidth, m_ContextMenuHeight }, { 0.28f, 0.28f, 0.30f, 1.0f });
                UIRenderer::DrawString("Delete", m_ContextMenuX + 10.0f, m_ContextMenuY + 19.0f, { 0.95f, 0.72f, 0.72f, 1.0f });
            }
        }

        bool AssetBrowserPanel::OnEvent(Event& e)
        {
            if (!m_IsVisible)
                return false;

            if (e.GetEventType() == EventType::MouseScrolled)
            {
                auto& se = static_cast<MouseScrolledEvent&>(e);
                m_ScrollState.ApplyScroll(se.GetYOffset() * -1.0f);
                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::KeyPressed)
            {
                auto& ke = static_cast<KeyPressedEvent&>(e);
                if (ke.GetKeyCode() == 46 && DeleteSelectedAsset())
                {
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                auto& me = static_cast<MouseButtonPressedEvent&>(e);
                if (me.GetButton() == 0 && m_ContextMenuVisible)
                {
                    if (IsContextMenuPoint(me.GetX(), me.GetY()))
                    {
                        DeleteSelectedAsset();
                        m_ContextMenuVisible = false;
                        e.Handled = true;
                        return true;
                    }

                    m_ContextMenuVisible = false;
                }

                if (me.GetButton() == 0 && IsScrollbarPoint(me.GetX(), me.GetY()))
                {
                    m_IsDraggingScrollbar = true;
                    m_IsMouseDownOnEntry = false;
                    m_DragMouseStartY = me.GetY();
                    m_DragScrollStartY = m_ScrollState.ScrollY;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && IsContentPoint(me.GetX(), me.GetY()))
                {
                    int index = GetEntryIndexAt(me.GetX(), me.GetY());

                    if (index >= 0 && index < (int)m_Entries.size())
                    {
                        auto now = std::chrono::steady_clock::now();
                        bool isDoubleClick = m_LastClickedIndex == index &&
                            (now - m_LastClickTime) < std::chrono::milliseconds(450);

                        m_SelectedIndex = index;
                        if (isDoubleClick)
                        {
                            ActivateEntry(m_Entries[index]);
                            m_IsMouseDownOnEntry = false;
                        }
                        else
                        {
                            m_IsMouseDownOnEntry = true;
                            m_DragEntryIndex = index;
                            m_DragStartX = me.GetX();
                            m_DragStartY = me.GetY();
                        }

                        m_LastClickedIndex = index;
                        m_LastClickTime = now;
                    }

                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 1 && IsContentPoint(me.GetX(), me.GetY()))
                {
                    int index = GetEntryIndexAt(me.GetX(), me.GetY());
                    if (index >= 0 && index < (int)m_Entries.size())
                    {
                        m_SelectedIndex = index;
                        const auto& entry = m_Entries[index];
                        m_ContextMenuVisible = entry.Type != AssetType::Folder && entry.Type != AssetType::Unknown;
                        m_ContextMenuX = (std::min)(me.GetX(), m_CalculatedPos.x + m_CalculatedSize.x - m_ContextMenuWidth - 4.0f);
                        m_ContextMenuY = (std::min)(me.GetY(), m_CalculatedPos.y + m_CalculatedSize.y - m_ContextMenuHeight - 4.0f);
                    }
                    else
                    {
                        m_ContextMenuVisible = false;
                    }

                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float trackSpace = m_ScrollState.ViewportHeight - m_ScrollState.GetThumbHeight();

                if (trackSpace > 0.0f)
                {
                    float deltaY = me.GetY() - m_DragMouseStartY;
                    float scrollDelta = (deltaY / trackSpace) * m_ScrollState.GetMaxScroll();
                    m_ScrollState.ScrollY = std::clamp(m_DragScrollStartY + scrollDelta, 0.0f, m_ScrollState.GetMaxScroll());
                }

                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsMouseDownOnEntry && m_DragEntryIndex >= 0)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float dx = me.GetX() - m_DragStartX;
                float dy = me.GetY() - m_DragStartY;

                if ((dx * dx + dy * dy) > 36.0f)
                {
                    m_IsDraggingAsset = true;
                }

                if (m_IsDraggingAsset)
                {
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseButtonReleased)
            {
                auto& me = static_cast<MouseButtonReleasedEvent&>(e);
                if (me.GetButton() == 0 && m_IsDraggingScrollbar)
                {
                    m_IsDraggingScrollbar = false;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_IsMouseDownOnEntry)
                {
                    if (m_IsDraggingAsset && m_DragEntryIndex >= 0 && m_DragEntryIndex < (int)m_Entries.size())
                    {
                        const auto& entry = m_Entries[m_DragEntryIndex];
                        if (entry.Type != AssetType::Folder && m_OnAssetDropped)
                        {
                            m_OnAssetDropped(entry.Path.string(), GetTypeKey(entry.Type), me.GetX(), me.GetY());
                        }
                    }

                    m_IsMouseDownOnEntry = false;
                    m_IsDraggingAsset = false;
                    m_DragEntryIndex = -1;
                    e.Handled = true;
                    return true;
                }
            }

            return WindowPanel::OnEvent(e);
        }
    }
}
