#pragma once
#include <string>
#include "Core.h"

namespace CCEngine
{
    class CC_API PlatformUtils
    {
    public:
        // 파일 저장 창을 띄웁니다. (filter 예시: "Scene Files (*.ccscene)\0*.ccscene\0")
        static std::string SaveFile(const char* filter, const char* initialDir = nullptr);

        // (미리 만들어두는) 파일 열기 창
        static std::string OpenFile(const char* filter, const char* initialDir = nullptr);
    };
}