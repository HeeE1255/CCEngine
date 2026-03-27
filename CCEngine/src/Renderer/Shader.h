#pragma once
#include "Core.h"
#include <string>
#include "Renderer/Buffer.h"

namespace CCEngine
{
    class CC_API Shader
    {
    public:
        virtual ~Shader() = default;

        // 셰이더를 GPU 파이프라인에 적용
        virtual void Bind() const = 0;

        // 셰이더 해제
        virtual void Unbind() const = 0;

        virtual void BindLayout(const BufferLayout& layout) = 0;

        // 파일 경로를 받아서 셰이더를 생성하는 팩토리 함수
        static Shader* Create(const std::string& filepath);
    };
}