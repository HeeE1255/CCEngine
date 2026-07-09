#pragma once

#include "Core.h"

namespace CCEngine
{
    class CC_API ScriptCompiler
    {
    public:
        static void RequestCompile();
        static void Update();
        static bool IsCompiling();
    };
}
