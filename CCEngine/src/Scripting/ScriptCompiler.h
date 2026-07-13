#pragma once

#include "Core.h"

namespace CCEngine
{
    class CC_API ScriptCompiler
    {
    public:
        static void RequestCompile();
        static bool Update();
        static bool IsCompiling();
    };
}
