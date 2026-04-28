#include "Renderer3D.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/RenderCommand.h" // DX11 RHI의 RenderCommand 호출용!
#include "Utils/MathUtils.h"

namespace CCEngine
{
    // HLSL의 b0 레지스터와 매칭 (카메라)
    struct CameraData
    {
        DirectX::XMMATRIX ViewProjection;
    };

    struct SceneBufferData
    {
        LightInfo Lights[4];
        int LightCount;
        float Padding[3];
    };

    // HLSL의 b1 레지스터와 매칭 (오브젝트)
    struct TransformData // 총 96바이트 
    {
        DirectX::XMMATRIX Transform; // 64 byte
        DirectX::XMFLOAT4 BaseColor; // 16 byte
        int EntityID;                // 4 byte
        int HasAnimation; // 4 byte
        float padding[2];
    };

    // HLSL의 b2 레지스터와 매칭 (뼈대 애니메이션)
    struct BoneData // 12800 바이트 (64 bytes * 512 matrices)
    {
        DirectX::XMMATRIX BoneMatrices[512];
    };

    Renderer3D::RenderData* Renderer3D::s_Data = new Renderer3D::RenderData();

    void Renderer3D::Init()
    {
        s_Data->Base3DShader.reset(Shader::Create("assets/shaders/Base3D.hlsl"));

        s_Data->DefaultWhiteTexture.reset(Texture2D::Create(1, 1));
        uint32_t whiteTextureData = 0xffffffff;
        s_Data->DefaultWhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        s_Data->DefaultMeshes[MeshComponent::MeshType::Cube] = MeshFactory::CreateCube();
        s_Data->DefaultMeshes[MeshComponent::MeshType::Plane] = MeshFactory::CreatePlane();
        s_Data->DefaultMeshes[MeshComponent::MeshType::Sphere] = MeshFactory::CreateSphere();

        s_Data->CameraConstantBuffer.reset(ConstantBuffer::Create(sizeof(CameraData)));
        s_Data->TransformConstantBuffer.reset(ConstantBuffer::Create(sizeof(TransformData)));

        s_Data->BoneConstantBuffer.reset(ConstantBuffer::Create(sizeof(BoneData)));
        s_Data->SceneConstantBuffer.reset(ConstantBuffer::Create(sizeof(SceneBufferData)));
    }

    void Renderer3D::Shutdown()
    {
        // 스마트 포인터를 사용했으므로 s_Data를 지우면 리소스가 자동 해제
        delete s_Data;
    }

    void Renderer3D::BeginScene(const PerspectiveCamera& camera, const SceneLightData& lightData)
    {
        // 깊이버퍼 활성
        RenderCommand::SetDepthTest(true);

        // 매 프레임 시작 시, 카메라 행렬(ViewProjection)을 계산하여 상수 버퍼에 세팅!
        CameraData camData;
        camData.ViewProjection = CCEngine::Math::GetMatrixForShader(camera.GetViewProjectionMatrix());

        s_Data->CameraConstantBuffer->SetData(&camData, sizeof(CameraData));

        // b0 (슬롯 0번)에 바인딩
        s_Data->CameraConstantBuffer->Bind(0);

        SceneBufferData sceneData;

        // Scene에서 넘어온 배열 데이터를 버퍼용 구조체로 그대로 복사
        for (int i = 0; i < 4; i++)
        {
            sceneData.Lights[i] = lightData.Lights[i];
        }
        sceneData.LightCount = lightData.LightCount;

        // 패딩 값 초기화 (쓰레기값 방지)
        sceneData.Padding[0] = 0.0f;
        sceneData.Padding[1] = 0.0f;
        sceneData.Padding[2] = 0.0f;

        s_Data->SceneConstantBuffer->SetData(&sceneData, sizeof(SceneBufferData));
        s_Data->SceneConstantBuffer->Bind(3); // b3 레지스터
    }

    void Renderer3D::EndScene()
    {
        // 빈자리
    }

    // 일반 정적 메쉬 드로우 콜
    void Renderer3D::DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID)
    {
        if (!mesh)
        {
            return;
        }

        // 1. 현재 그릴 오브젝트의 데이터를 세팅
        TransformData transformData;
        transformData.Transform = CCEngine::Math::GetMatrixForShader(transform);
        transformData.BaseColor = color;
        transformData.EntityID = entityID;
        transformData.HasAnimation = 0;

        // b1 (슬롯 1번)에 상수 버퍼 데이터 덮어쓰고 바인딩
        s_Data->TransformConstantBuffer->SetData(&transformData, sizeof(TransformData));
        s_Data->TransformConstantBuffer->Bind(1);

        if (texture)
        {
            texture->Bind(0);
        }
        else
        {
            s_Data->DefaultWhiteTexture->Bind(0);
        }

        // 2. 셰이더 활성화 및 레이아웃 설정
        s_Data->Base3DShader->Bind();
        s_Data->Base3DShader->BindLayout(mesh->GetVertexBuffer()->GetLayout());

        // 3. 메쉬(정점 버퍼, 인덱스 버퍼) 활성화
        mesh->Bind();

        // 4. RHI에게 인덱스 버퍼를 건네주며 그리라고 명령 하달
        RenderCommand::DrawIndexed(mesh->GetIndexBuffer().get());
    }

    //  뼈대가 있는 스킨드 메쉬 드로우 콜
    void Renderer3D::DrawSkinnedMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID, const std::vector<DirectX::XMMATRIX>& boneMatrices)
    {
        if (!mesh)
        {
            return;
        }

        // 1. 일반 트랜스폼 데이터 세팅
        TransformData transformData;
        transformData.Transform = CCEngine::Math::GetMatrixForShader(transform);
        transformData.BaseColor = color;
        transformData.EntityID = entityID;
        transformData.HasAnimation = 1;

        s_Data->TransformConstantBuffer->SetData(&transformData, sizeof(TransformData));
        s_Data->TransformConstantBuffer->Bind(1);

        // 2. 뼈대 데이터 세팅
        BoneData boneData;
        size_t copyCount = boneMatrices.size() > 512 ? 512 : boneMatrices.size();

        for (size_t i = 0; i < copyCount; ++i)
        {
            // 셰이더로 넘기기 위해 전치(Transpose) 처리
            boneData.BoneMatrices[i] = CCEngine::Math::GetMatrixForShader(boneMatrices[i]);
        }

        DirectX::XMMATRIX identityMatrix = DirectX::XMMatrixIdentity();
        for (size_t i = copyCount; i < 512; ++i)
        {
            boneData.BoneMatrices[i] = CCEngine::Math::GetMatrixForShader(identityMatrix);
        }

        // b2 (슬롯 2번)에 뼈대 상수 버퍼 바인딩
        s_Data->BoneConstantBuffer->SetData(&boneData, sizeof(BoneData));
        s_Data->BoneConstantBuffer->Bind(2);

        if (texture)
        {
            texture->Bind(0);
        }
        else
        {
            s_Data->DefaultWhiteTexture->Bind(0);
        }

        s_Data->Base3DShader->Bind();
        s_Data->Base3DShader->BindLayout(mesh->GetVertexBuffer()->GetLayout());

        mesh->Bind();

        RenderCommand::DrawIndexed(mesh->GetIndexBuffer().get());
    }
}