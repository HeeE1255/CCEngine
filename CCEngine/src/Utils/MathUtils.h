#pragma once
#include <DirectXMath.h>
#include "Renderer/RendererAPI.h" // 현재 API 확인용

namespace CCEngine
{
    namespace Math
    {
        struct Ray {
            DirectX::XMFLOAT3 Origin;
            DirectX::XMFLOAT3 Direction;
        };

        class MathUtils {

        public:
            // 마우스 좌표를 3D Ray로 변환하는 마법의 함수
            static Ray ScreenPosToWorldRay(float mouseX, float mouseY, float viewportWidth, float viewportHeight,
                DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix)
            {
                // 1. 화면 좌표(0~Width)를 NDC 공간(-1.0 ~ 1.0)으로 변환 (Y축은 뒤집음)
                float ndcX = (2.0f * mouseX) / viewportWidth - 1.0f;
                float ndcY = 1.0f - (2.0f * mouseY) / viewportHeight;

                // 2. 투영(Projection) 공간에서의 광선 방향 설정
                // Z가 1.0인 곳을 향해 쏨 (DirectX 기준)
                DirectX::XMVECTOR rayClip = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

                // 3. View * Proj 매트릭스의 역행렬 계산
                DirectX::XMMATRIX viewProj = viewMatrix * projMatrix;
                DirectX::XMVECTOR det;
                DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(&det, viewProj);

                // 4. 역행렬을 곱해서 World 공간의 좌표로 변환
                DirectX::XMVECTOR rayWorld = DirectX::XMVector4Transform(rayClip, invViewProj);

                // W 나누기 (Perspective Divide)
                float w = DirectX::XMVectorGetW(rayWorld);
                if (w != 0.0f) {
                    rayWorld = DirectX::XMVectorScale(rayWorld, 1.0f / w);
                }

                // 5. 카메라 위치(Origin)와 광선 방향(Direction) 계산
                DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, viewMatrix);
                DirectX::XMVECTOR rayOrigin = invView.r[3]; // 뷰 매트릭스 역행렬의 4번째 행이 카메라 위치

                DirectX::XMVECTOR rayDir = DirectX::XMVectorSubtract(rayWorld, rayOrigin);
                rayDir = DirectX::XMVector3Normalize(rayDir);

                Ray result;
                DirectX::XMStoreFloat3(&result.Origin, rayOrigin);
                DirectX::XMStoreFloat3(&result.Direction, rayDir);

                return result;
            }

            // ====================================================================
            // 1. ImGuizmo 및 C++ 내부 연산용 (API 상관없이 전치 불필요)
            // ====================================================================
            static inline void ToFloat16(const DirectX::XMMATRIX& matrix, float* outFloat16)
            {
                // DirectXMath 행렬을 float[16] 배열로 깔끔하게 변환
                DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)outFloat16, matrix);
            }

            // ====================================================================
            // 2. 셰이더(GPU) 업로드용 (API별 예외 처리 적용!)
            // ====================================================================
            static inline DirectX::XMMATRIX GetMatrixForShader(const DirectX::XMMATRIX& matrix)
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

            // 두 3D 직선(Ray)이 가장 가까워지는 점의 매개변수(t1, t2)를 구하는 함수
            // ray1: 기즈모 축 (Origin, Dir) / ray2: 마우스 광선 (Origin, Dir)
            // 반환값: 두 선 사이의 최단 거리
            static float ClosestPointBetweenTwoLines(const Ray& ray1, const Ray& ray2, float& out_t1, float& out_t2)
            {
                DirectX::XMVECTOR p1 = DirectX::XMLoadFloat3(&ray1.Origin);
                DirectX::XMVECTOR d1 = DirectX::XMLoadFloat3(&ray1.Direction);
                DirectX::XMVECTOR p2 = DirectX::XMLoadFloat3(&ray2.Origin);
                DirectX::XMVECTOR d2 = DirectX::XMLoadFloat3(&ray2.Direction);

                DirectX::XMVECTOR r = DirectX::XMVectorSubtract(p1, p2);

                float a = DirectX::XMVectorGetX(DirectX::XMVector3Dot(d1, d1)); // 항상 1 (정규화된 경우)
                float e = DirectX::XMVectorGetX(DirectX::XMVector3Dot(d2, d2)); // 항상 1
                float f = DirectX::XMVectorGetX(DirectX::XMVector3Dot(d2, r));
                float c = DirectX::XMVectorGetX(DirectX::XMVector3Dot(d1, r));
                float b = DirectX::XMVectorGetX(DirectX::XMVector3Dot(d1, d2));

                float denom = a * e - b * b;

                // 두 선이 평행한 경우 예외 처리
                if (denom != 0.0f) {
                    out_t1 = (b * f - c * e) / denom;
                }
                else {
                    out_t1 = 0.0f;
                }
                out_t2 = (out_t1 * b) + f;

                // 최단 거리에 있는 두 점 계산
                DirectX::XMVECTOR c1 = DirectX::XMVectorAdd(p1, DirectX::XMVectorScale(d1, out_t1));
                DirectX::XMVECTOR c2 = DirectX::XMVectorAdd(p2, DirectX::XMVectorScale(d2, out_t2));

                // 두 점 사이의 거리 반환
                DirectX::XMVECTOR distVec = DirectX::XMVectorSubtract(c1, c2);
                return DirectX::XMVectorGetX(DirectX::XMVector3Length(distVec));
            }

            static bool RayPlaneIntersection(const Ray& ray, DirectX::XMVECTOR planePos, DirectX::XMVECTOR planeNormal, float& out_t)
            {
                DirectX::XMVECTOR rayOrigin = DirectX::XMLoadFloat3(&ray.Origin);
                DirectX::XMVECTOR rayDir = DirectX::XMLoadFloat3(&ray.Direction);

                float denom = DirectX::XMVectorGetX(DirectX::XMVector3Dot(planeNormal, rayDir));
                if (std::abs(denom) > 0.0001f) // 평면과 평행하지 않을 때
                {
                    out_t = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(planePos, rayOrigin), planeNormal)) / denom;
                    return out_t >= 0.0f;
                }
                return false;
            }

        };
    }
}