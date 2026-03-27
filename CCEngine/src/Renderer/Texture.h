#pragma once
#include <string>
#include "Core.h"

namespace CCEngine
{
    class CC_API Texture2D
    {
    public:
        virtual ~Texture2D() = default;

        virtual void Bind(uint32_t slot = 0) const = 0;
        virtual void Unbind(uint32_t slot = 0) const = 0;

        // 텍스처 생성 함수: 파일 경로를 받아서 텍스처 객체를 생성하는 정적 함수
        static Texture2D* Create(const std::string& path);
    };
}