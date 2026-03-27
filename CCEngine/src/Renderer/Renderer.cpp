#include "Renderer/Renderer.h"
#include "Renderer/RenderCommand.h" // 다이렉트X 헤더는 대신 추상화된 커맨드 호출

namespace CCEngine
{
    // 배경색을 기억해 둘 내부 변수
    static float s_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

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
        // 지금은 일단 비워둡니다!
    }

    void Renderer::EndScene()
    {
        // 렌더링 마무리 작업이 들어갈 곳입니다.
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
        // 기존의 하드코딩된 DX11 DeviceContext와 Topology 호출을 전부 지우고 RHI로 위임!
        RenderCommand::DrawIndexed(indexBuffer);
    }

    void Renderer::OnWindowResize(uint32_t width, uint32_t height)
    {
        // 1. 그래픽 컨텍스트(스왑체인) 리사이즈! (RHI가 알아서 DX11/Vulkan 호출)
        RenderCommand::ResizeContext(width, height);

        // 2. 뷰포트(Viewport) 영역 재설정!
        RenderCommand::SetViewport(0, 0, width, height);
    }
}