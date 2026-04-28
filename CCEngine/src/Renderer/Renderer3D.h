#pragma once
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Shader.h"
#include "Scene/Components.h" // MeshComponent::MeshType 사용을 위해 추가
#include <memory>
#include <unordered_map>      // 맵 사용을 위해 추가

namespace CCEngine
{
    struct LightInfo
    {
        DirectX::XMFLOAT3 Direction;
        float Intensity = 1.0f;
        DirectX::XMFLOAT3 Color;
        float Padding = 0.0f;
    };

    struct SceneLightData
    {
        LightInfo Lights[4]; // 최대 4개의 조명 지원
        int LightCount = 0;  // 현재 씬에 있는 조명 개수
        float Padding[3];    // 16바이트 정렬용 패딩
    };

    class CC_API Renderer3D
    {
    public:
        static void Init();
        static void Shutdown();

        static void BeginScene(const PerspectiveCamera& camera, const SceneLightData& lightData);
        static void EndScene();

        // 기본 메시 그리기 (MeshComponent::MeshType이 Custom이 아닌 경우)
        static void DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID = -1);
        // 뼈대 애니메이션이 있는 스킨드 메쉬 드로우 콜
        static void DrawSkinnedMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID, const std::vector<DirectX::XMMATRIX>& boneMatrices);
    private:
        struct RenderData
        {
            std::shared_ptr<Shader> Base3DShader;
            std::shared_ptr<ConstantBuffer> CameraConstantBuffer;    // View, Proj 행렬용
            std::shared_ptr<ConstantBuffer> TransformConstantBuffer; // Transform 행렬 및 색상용
            std::unordered_map<MeshComponent::MeshType, std::shared_ptr<Mesh>> DefaultMeshes; // 기본 도형들을 저장하는 맵 (MeshType -> Mesh)
            std::shared_ptr<Texture2D> DefaultWhiteTexture;// 텍스처가 없는 경우 사용할 기본 흰색 텍스처
            std::shared_ptr<ConstantBuffer> BoneConstantBuffer; // 뼈대 애니메이션용 상수 버퍼
            std::shared_ptr<ConstantBuffer> SceneConstantBuffer; // (조명) 버퍼
        };

        static RenderData* s_Data;
    };
}