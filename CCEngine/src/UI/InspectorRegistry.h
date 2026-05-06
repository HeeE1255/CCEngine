#pragma once
#include "UI/Widget.h"
#include "Scene/Entity.h"
#include <functional>
#include <unordered_map>
#include <typeindex>

namespace CCEngine {
    namespace UI {

        class InspectorRegistry
        {
        public:
            // 컴포넌트를 그리는 람다 함수를 타입별로 등록합니다.
            template<typename T>
            static void RegisterComponent(std::function<void(Widget* parent, CCEngine::Entity entity, T& component)> drawFunction)
            {
                s_DrawFunctions[typeid(T)] = [drawFunction](Widget* parent, CCEngine::Entity entity) {
                    if (entity.HasComponent<T>()) {
                        drawFunction(parent, entity, entity.GetComponent<T>());
                    }
                    };
            }

            // 선택된 엔티티가 가진 모든 컴포넌트를 인스펙터 패널에 쫙 뿌려줍니다.
            static void DrawAllComponents(Widget* parent, CCEngine::Entity entity)
            {
                if (!entity) return;

                for (auto& [type, drawFunc] : s_DrawFunctions)
                {
                    drawFunc(parent, entity);
                }
            }

        private:
            // 타입 인덱스를 키로 사용하여 렌더링 함수들을 저장하는 맵
            inline static std::unordered_map<std::type_index, std::function<void(Widget*, CCEngine::Entity)>> s_DrawFunctions;
        };

        // (호환성을 위해 ComponentRegistry 이름도 InspectorRegistry와 동일하게 매핑)
        using ComponentRegistry = InspectorRegistry;
    }
}