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

        // 데이터 덮어쓰기 및 ImGui 출력을 위한 함수
        virtual void SetData(void* data, uint32_t size) = 0;
        virtual void* GetRendererID() const = 0;

        // 기존 파일 경로 로드 함수
        static Texture2D* Create(const std::string& path);

        // 가로, 세로 크기만으로 빈 텍스처를 생성하는 함수
        static Texture2D* Create(uint32_t width, uint32_t height);

		// CPU 메모리의 픽셀 데이터를 이용해 텍스처를 생성하는 함수 (예: 폰트 아틀라스)
        static Texture2D* Create(uint32_t width, uint32_t height, void* data);
    };
}