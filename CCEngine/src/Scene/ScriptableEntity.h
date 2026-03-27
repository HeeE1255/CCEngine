#pragma once
#include "Scene/Entity.h"

namespace CCEngine
{
    class CC_API ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        // 스크립트에서 엔티티의 컴포넌트에 접근할 수 있도록 템플릿 함수를 제공
        template<typename T>
        T& GetComponent()
        {
            return m_Entity.GetComponent<T>();
        }

    protected:
        // 유니티의 Start(), Update(), OnDestroy() 와 같음
        virtual void OnCreate() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnDestroy() {}

    private:
        Entity m_Entity;

        // Scene이 ScriptableEntity의 m_Entity에 접근할 수 있도록 허락
        friend class Scene;
    };
}