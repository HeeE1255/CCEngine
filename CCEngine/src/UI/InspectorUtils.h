#pragma once
#include "Core.h"
#include "UI/InspectorItem.h"
#include "UI/DragFloat.h"
#include "UI/DragFloat3.h"
#include "UI/DragFloat4.h"
#include <functional>
#include <string>
#include <DirectXMath.h>

namespace CCEngine {
    namespace UI {

        class InspectorUtils
        {
        public:
            // 1개의 Float (ex: Light Intensity)
            static void AddDragFloat(InspectorItem* item, const std::string& name, const std::string& label,
                std::function<float()> getter, std::function<void(float)> setter);

            // 3개의 Float (ex: Position, Rotation)
            static void AddDragFloat3(InspectorItem* item, const std::string& name, const std::string& label,
                std::function<DirectX::XMFLOAT3()> getter, std::function<void(DirectX::XMFLOAT3)> setter);

            // 4개의 Float + Color Block (ex: Albedo Color)
            static void AddColor4(InspectorItem* item, const std::string& name, const std::string& label,
                std::function<DirectX::XMFLOAT4()> getter, std::function<void(DirectX::XMFLOAT4)> setter);

            // 3개의 Float(RGB)를 받지만 UI는 Color Block이 있는 4칸짜리(DragFloat4)를 빌려 쓰는 편의 함수! (LightColor 용)
            static void AddColor3(InspectorItem* item, const std::string& name, const std::string& label,
                std::function<DirectX::XMFLOAT3()> getter, std::function<void(DirectX::XMFLOAT3)> setter);

            static void InitStandardComponents();
        };

    }
}