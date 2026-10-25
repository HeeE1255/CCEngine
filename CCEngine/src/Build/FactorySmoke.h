#pragma once

#include "Core.h"

namespace CCEngine
{
    // 공통 RHI 팩토리가 선택된 백엔드를 실제로 생성하는지 확인한다.
    // scope는 buffer, texture, framebuffer 또는 all이다.
    CC_API int RunFactorySmokeCheck(const char* backend, const char* scope);
}
