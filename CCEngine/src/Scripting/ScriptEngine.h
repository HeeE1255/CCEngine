#pragma once

#include "Core.h"
#include <cstdint>

namespace CCEngine
{
    class Scene;

    class CC_API ScriptEngine
    {
    public:
        static bool Start(Scene* scene);
        static void Stop();
        static bool CreateInstance(uint32_t entityID, const char* className);
        static void DestroyInstance(uint32_t entityID);
        static void UpdateInstance(uint32_t entityID, float deltaTime);
        static bool IsRunning();
    };
}
