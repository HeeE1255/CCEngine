#pragma once
#include "Core.h" // CC_API (__declspec) 매크로가 정의된 파일

namespace CCEngine {

    // ==========================================
    // 1. 창 크기 변경 이벤트 (WindowResizeEvent)
    // ==========================================
    // 윈도우 OS가 "창 크기 변했어!" 라고 알려준 가로/세로 픽셀 값을 
    // 엔진 내부로 안전하게 전달하기 위한 포장 박스
    class CC_API WindowResizeEvent
    {
    public:
        WindowResizeEvent(unsigned int width, unsigned int height)
            : m_Width(width), m_Height(height) {
        }

        unsigned int GetWidth() const { return m_Width; }
        unsigned int GetHeight() const { return m_Height; }

    private:
        unsigned int m_Width;
        unsigned int m_Height;
    };


    // ==========================================
    // 2. 창 닫기 이벤트 (WindowCloseEvent)
    // ==========================================
    // 오른쪽 위 X 버튼을 눌렀을 때 엔진을 안전하게 종료시키기 위한 박스
    class CC_API WindowCloseEvent
    {
    public:
        WindowCloseEvent() = default;
    };

}