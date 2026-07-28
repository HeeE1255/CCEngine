#pragma once

#include "Core.h"
#include <cstdint>

namespace CCEngine
{
    class Scene;
    struct ScriptComponent;

    enum class ScriptLifecycleEvent : int
    {
        Awake = 0,
        OnEnable,
        Start,
        FixedUpdate,
        Update,
        LateUpdate,
        OnDisable,
        OnDestroy
    };

    enum class ScriptPhysicsEvent : int
    {
        OnCollisionEnter2D = 0,
        OnCollisionStay2D,
        OnCollisionExit2D,
        OnTriggerEnter2D,
        OnTriggerStay2D,
        OnTriggerExit2D
    };

    class CC_API ScriptEngine
    {
    public:
        static bool Start(Scene* scene);
        static void Stop();
        static bool CreateInstance(uint32_t entityID, const ScriptComponent& script);
        static void DestroyInstance(uint32_t entityID);
        static void InvokeLifecycle(uint32_t entityID, ScriptLifecycleEvent eventType, float deltaTime = 0.0f);
        static void InvokePhysicsEvent(uint32_t entityID, ScriptPhysicsEvent eventType, uint32_t otherEntityID);
        static void UpdateInstance(uint32_t entityID, float deltaTime);
        static bool IsRunning();
    };
}
