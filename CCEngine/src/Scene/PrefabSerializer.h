#pragma once

#include "Core.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include <string>

namespace CCEngine
{
    class CC_API PrefabSerializer
    {
    public:
        static bool Serialize(Scene* scene, Entity rootEntity, const std::string& filepath);
        static Entity Deserialize(Scene* scene, const std::string& filepath);
    };
}
