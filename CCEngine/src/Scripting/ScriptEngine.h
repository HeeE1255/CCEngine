#pragma once

#include "Core.h"
#include <cstdint>

namespace CCEngine
{
    class Scene;
    struct ScriptComponent;

    class CC_API ScriptEngine
    {
    public:
        static bool Start(Scene* scene);
        static void Stop();
        static bool CreateInstance(uint32_t entityID, const ScriptComponent& script);
        static void DestroyInstance(uint32_t entityID);
        static void UpdateInstance(uint32_t entityID, float deltaTime);
        static bool IsRunning();
    };
}
