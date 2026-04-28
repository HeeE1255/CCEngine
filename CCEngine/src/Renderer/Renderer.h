#pragma once
#include "Core.h"
#include "Renderer/Shader.h"
#include "Renderer/Buffer.h"

namespace CCEngine
{
    class CC_API Renderer
    {
    public:
        // [🔥 추가] 엔진 렌더링 시스템 전체 초기화/해제
        static void Init();
        static void Shutdown();

        // 1. 화면 초기화와 클리어
        static void SetClearColor(float r, float g, float b, float a);
        static void Clear();

        // 2. 씬 시작과 끝 (렌더링 세션 관리)
        static void BeginScene();
        static void EndScene();

        // 3. 드로우 콜 (실제 그리기 명령)
        static void Submit(Shader* shader, VertexBuffer* vertexBuffer, IndexBuffer* indexBuffer);

        // 4. 윈도우 리사이즈 이벤트 처리
        static void OnWindowResize(unsigned int width, unsigned int height);
    };
}