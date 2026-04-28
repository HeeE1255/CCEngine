#pragma once
#include <DirectXMath.h>
#include "Renderer/RendererAPI.h" // 현재 API 확인용

namespace CCEngine::Math
{
    // ====================================================================
    // 1. ImGuizmo 및 C++ 내부 연산용 (API 상관없이 전치 불필요)
    // ====================================================================
    inline void ToFloat16(const DirectX::XMMATRIX& matrix, float* outFloat16)
    {
        // DirectXMath 행렬을 float[16] 배열로 깔끔하게 변환
        DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)outFloat16, matrix);
    }

    // ====================================================================
    // 2. 셰이더(GPU) 업로드용 (API별 예외 처리 적용!)
    // ====================================================================
    inline DirectX::XMMATRIX GetMatrixForShader(const DirectX::XMMATRIX& matrix)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::DirectX11:
            // DX11 (HLSL)은 GPU로 보낼 때 무조건 전치(Transpose)가 필요
            return DirectX::XMMatrixTranspose(matrix);

        case RendererAPI::API::OpenGL:
            // OpenGL은 나중에 C++ 코어 수학을 뭘 쓰냐에 따라 다르지만, 
            // 보통 그대로 보내거나 glUniformMatrix4fv에서 GL_TRUE/FALSE로 처리
            return matrix;

        case RendererAPI::API::Vulkan:
            // Vulkan (GLSL/SPIR-V) 역시 구조에 따라 맞춤 처리 가능
            return matrix;

        default:
            return matrix;
        }
    }
}