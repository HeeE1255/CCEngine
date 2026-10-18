#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h" 

// [🔥 추가] 하위 렌더링 시스템들을 초기화하기 위해 헤더 포함
#include "Renderer/RenderState.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/UIRenderer.h"

namespace CCEngine
{
    // 배경색을 기억해 둘 내부 변수
    static float s_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    static bool s_Initialized = false;

    // ==========================================================
    // [🔥 추가] 렌더러 전체 초기화 및 메모리 해제
    // ==========================================================
    void Renderer::Init()
    {
        if (s_Initialized)
            return;
        try
        {
            RenderCommand::Init();
            RenderState::Init();
            Renderer2D::Init();
            UIRenderer::Init();
            Renderer3D::Init();
            s_Initialized = true;
        }
        catch (...)
        {
            // 뒤 단계가 실패해도 앞에서 만든 폰트/버퍼를 남기지 않는다.
            Shutdown();
            throw;
        }
    }

    void Renderer::Shutdown()
    {
        Renderer3D::Shutdown();
        UIRenderer::Shutdown();
        Renderer2D::Shutdown();  //
        RenderState::Shutdown(); // 
        RenderCommand::Shutdown();
        s_Initialized = false;
    }
    // ==========================================================

    void Renderer::SetClearColor(float r, float g, float b, float a)
    {
        s_ClearColor[0] = r;
        s_ClearColor[1] = g;
        s_ClearColor[2] = b;
        s_ClearColor[3] = a;

        // RHI 사령탑에게 배경색 세팅 명령을 하달
        RenderCommand::SetClearColor(r, g, b, a);
    }

    void Renderer::Clear()
    {
        // RHI 사령탑에게 화면을 지우라고 명령
        RenderCommand::Clear();
    }

    void Renderer::BeginScene()
    {
        // 나중에 3D 카메라 행렬(View, Projection) 등을 여기서 세팅
    }

    void Renderer::EndScene()
    {
        // 렌더링 마무리 작업이 들어갈 곳
    }

    void Renderer::Submit(Shader* shader, VertexBuffer* vertexBuffer, IndexBuffer* indexBuffer)
    {
        // 1. 셰이더와 버퍼를 GPU 파이프라인에 연결
        shader->Bind();
        vertexBuffer->Bind();
        indexBuffer->Bind();

        // 버퍼 레이아웃을 셰이더에 알려주는 단계 (셰이더가 정점 데이터를 어떻게 해석할지)
        shader->BindLayout(vertexBuffer->GetLayout());

        // 2. 드로우 콜: GPU에게 실제로 그리라고 명령
        RenderCommand::DrawIndexed(indexBuffer);
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height)
    {
        // Swapchain은 WM_SIZE에서 해당 창의 Context가 한 번만 변경한다.
        // 여기서 전역 Context를 다시 변경하면 보조 창을 잘못 리사이즈할 수 있다.
        if (width == 0 || height == 0)
            return;
        RenderCommand::SetViewport(0, 0, width, height);
    }
}
