#pragma once
#include "Core.h"
#include <DirectXMath.h>

namespace CCEngine
{
    class Window;

    namespace UI
    {
        class Widget;
        class WindowPanel;

        enum class DockDropMode {
            None, Left, Right, Top, Bottom
        };

        class CC_API DockManager
        {
        public:
            static void ClearPreview();
            static void DrawPreview(Window* renderWindow);
            static void UpdatePreview(WindowPanel* draggedPanel, float mouseX, float mouseY);
            static bool ApplyPreview(WindowPanel* draggedPanel);
            static void RemoveRelations(WindowPanel* panel);
            static bool ResizeRelation(WindowPanel* resizedPanel, int resizeMode, float deltaX, float deltaY);
            static void DetachFromAllWindowRoots(Widget* widget);
            static void DetachFromOtherWindowRoots(Widget* widget, Window* keepWindow);
            static void FillWindowRoot(Widget* widget);
            static void SetOwnerWindowRecursive(Widget* widget, Window* ownerWindow);
            static void CollapseDockRoot(Widget* dockRoot);
            static void NormalizeWindowPanelOwnership();

        private:
            static WindowPanel* FindDockTarget(Widget* root, WindowPanel* draggedPanel, float mouseX, float mouseY);
            static WindowPanel* FindDockTargetByOverlap(Widget* root, WindowPanel* draggedPanel, const DirectX::XMFLOAT4& draggedRect);
            static DockDropMode GetDockDropMode(WindowPanel* target, float mouseX, float mouseY);
            static DockDropMode GetDockDropModeFromOverlap(WindowPanel* target, const DirectX::XMFLOAT4& draggedRect);
            static DockDropMode GetRootDockDropMode(Window* targetWindow, float mouseX, float mouseY);
            static void ApplyAbsoluteRect(WindowPanel* panel, float x, float y, float width, float height);
            static void RegisterRelation(WindowPanel* first, WindowPanel* second, bool horizontal);
        };
    }
}
