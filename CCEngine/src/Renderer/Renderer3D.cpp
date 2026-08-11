#include "Renderer3D.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/RenderCommand.h" // DX11 RHI의 RenderCommand 호출용!
#include "Renderer/RuntimeShaderLibrary.h"
#include "Utils/MathUtils.h"

#include <algorithm>
#include <filesystem>

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

    constexpr uint32_t MaxMaterialColorProperties = 8;
    constexpr uint32_t MaxMaterialScalarSlots = 16;
    constexpr uint32_t MaxMaterialTextureSlots = 8;

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

    // HLSL의 b4 레지스터와 매칭되는 커스텀 Material 버퍼다.
    // b2는 스킨드 메쉬 본 행렬이 쓰고 있으므로 Material 값은 별도 슬롯에 둔다.
    // Visual Shader도 나중에 이 레이아웃을 기준으로 HLSL을 생성하면 같은 런타임 경로를 재사용할 수 있다.
    struct MaterialPropertyBufferData
    {
        DirectX::XMFLOAT4 AlbedoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 ColorProperties[MaxMaterialColorProperties] = {};
        DirectX::XMFLOAT4 ScalarProperties[4] = {};
        DirectX::XMFLOAT4 ToggleProperties[4] = {};
        DirectX::XMFLOAT4 SurfaceValues = { 0.5f, 0.0f, 0.0f, 0.0f }; // x: Roughness, y: Metallic
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
        s_Data->DefaultMeshes[MeshComponent::MeshType::Quad] = MeshFactory::CreateQuad();
        s_Data->DefaultMeshes[MeshComponent::MeshType::Capsule] = MeshFactory::CreateCapsule();
        s_Data->DefaultMeshes[MeshComponent::MeshType::Cylinder] = MeshFactory::CreateCylinder();
        s_Data->DefaultMeshes[MeshComponent::MeshType::Torus] = MeshFactory::CreateTorus();

        s_Data->CameraConstantBuffer.reset(ConstantBuffer::Create(sizeof(CameraData)));
        s_Data->TransformConstantBuffer.reset(ConstantBuffer::Create(sizeof(TransformData)));

        s_Data->BoneConstantBuffer.reset(ConstantBuffer::Create(sizeof(BoneData)));
        s_Data->SceneConstantBuffer.reset(ConstantBuffer::Create(sizeof(SceneBufferData)));
        s_Data->MaterialPropertyConstantBuffer.reset(ConstantBuffer::Create(sizeof(MaterialPropertyBufferData)));
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
		camData.ViewProjection = CCEngine::Math::MathUtils::GetMatrixForShader(camera.GetViewProjectionMatrix());
            //CCEngine::Math::MathUtils::GetMatrixForShader(camera.GetViewProjectionMatrix());

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

    namespace
    {
        void WriteScalar(MaterialPropertyBufferData& buffer, uint32_t scalarIndex, float value)
        {
            if (scalarIndex >= MaxMaterialScalarSlots)
                return;

            DirectX::XMFLOAT4& target = buffer.ScalarProperties[scalarIndex / 4];
            float* values = &target.x;
            values[scalarIndex % 4] = value;
        }

        void WriteToggle(MaterialPropertyBufferData& buffer, uint32_t toggleIndex, bool value)
        {
            if (toggleIndex >= MaxMaterialScalarSlots)
                return;

            DirectX::XMFLOAT4& target = buffer.ToggleProperties[toggleIndex / 4];
            float* values = &target.x;
            values[toggleIndex % 4] = value ? 1.0f : 0.0f;
        }

    }

    std::shared_ptr<Texture2D> Renderer3D::GetCachedMaterialTexture(const std::string& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
            return nullptr;

        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path);
        std::string key = normalizedPath.string();
        auto it = s_Data->MaterialPropertyTextureCache.find(key);
        if (it != s_Data->MaterialPropertyTextureCache.end())
            return it->second;

        std::shared_ptr<Texture2D> texture;
        texture.reset(Texture2D::Create(key));
        s_Data->MaterialPropertyTextureCache[key] = texture;
        return texture;
    }

    void Renderer3D::BindMaterialProperties(const MaterialAsset* material, const std::shared_ptr<Texture2D>& fallbackAlbedo)
    {
        MaterialPropertyBufferData buffer;
        const MaterialAsset* source = material;
        if (source)
        {
            buffer.AlbedoColor = source->AlbedoColor;
            buffer.SurfaceValues.x = source->Roughness;
            buffer.SurfaceValues.y = source->Metallic;
        }

        uint32_t colorIndex = 0;
        uint32_t scalarIndex = 0;
        uint32_t toggleIndex = 0;
        uint32_t textureIndex = 1;
        bool albedoSlotBound = false;

        if (source)
        {
            for (const auto& [name, value] : source->ShaderProperties)
            {
                if (value.Type == ShaderPropertyType::Color)
                {
                    if (name == "AlbedoColor")
                        buffer.AlbedoColor = value.Color;
                    else if (colorIndex < MaxMaterialColorProperties)
                        buffer.ColorProperties[colorIndex++] = value.Color;
                }
                else if (value.Type == ShaderPropertyType::Float)
                {
                    if (name == "Roughness")
                        buffer.SurfaceValues.x = value.FloatValue;
                    else if (name == "Metallic")
                        buffer.SurfaceValues.y = value.FloatValue;
                    else
                        WriteScalar(buffer, scalarIndex++, value.FloatValue);
                }
                else if (value.Type == ShaderPropertyType::Toggle)
                {
                    WriteToggle(buffer, toggleIndex++, value.BoolValue);
                }
                else if (value.Type == ShaderPropertyType::Texture2D)
                {
                    std::shared_ptr<Texture2D> texture = GetCachedMaterialTexture(value.TexturePath);
                    if (name == "AlbedoTexture")
                    {
                        if (texture)
                        {
                            texture->Bind(0);
                            albedoSlotBound = true;
                        }
                    }
                    else if (texture && textureIndex < MaxMaterialTextureSlots)
                    {
                        texture->Bind(textureIndex++);
                    }
                }
            }
        }

        s_Data->MaterialPropertyConstantBuffer->SetData(&buffer, sizeof(MaterialPropertyBufferData));
        s_Data->MaterialPropertyConstantBuffer->Bind(4);

        if (!albedoSlotBound && source && source->AlbedoTexture)
            source->AlbedoTexture->Bind(0);
        else if (!albedoSlotBound && fallbackAlbedo)
            fallbackAlbedo->Bind(0);
        else if (!albedoSlotBound)
            s_Data->DefaultWhiteTexture->Bind(0);
    }

    // 일반 정적 메쉬 드로우 콜
    void Renderer3D::DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID)
    {
        DrawMesh(transform, mesh, texture, color, nullptr, entityID);
    }

    void Renderer3D::DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, const MaterialAsset* material, int entityID)
    {
        if (!mesh)
        {
            return;
        }

        // 1. 현재 그릴 오브젝트의 데이터를 세팅
        TransformData transformData;
        transformData.Transform = CCEngine::Math::MathUtils::GetMatrixForShader(transform);
        transformData.BaseColor = color;
        transformData.EntityID = entityID;
        transformData.HasAnimation = 0;

        // b1 (슬롯 1번)에 상수 버퍼 데이터 덮어쓰고 바인딩
        s_Data->TransformConstantBuffer->SetData(&transformData, sizeof(TransformData));
        s_Data->TransformConstantBuffer->Bind(1);

        BindMaterialProperties(material, texture);

        // Material에 커스텀 HLSL이 연결된 경우 런타임 셰이더 캐시에서 가져온다.
        // 컴파일 실패 시에는 RuntimeShaderLibrary가 에러 셰이더를 반환해 씬에서 바로 보이게 한다.
        std::shared_ptr<Shader> runtimeShader = material ? RuntimeShaderLibrary::GetShaderForMaterial(*material) : nullptr;
        Shader* activeShader = runtimeShader && runtimeShader->IsValid()
            ? runtimeShader.get()
            : s_Data->Base3DShader.get();

        // 2. 셰이더 활성화 및 레이아웃 설정
        activeShader->Bind();
        activeShader->BindLayout(mesh->GetVertexBuffer()->GetLayout());

        // 3. 메쉬(정점 버퍼, 인덱스 버퍼) 활성화
        mesh->Bind();

        // 4. RHI에게 인덱스 버퍼를 건네주며 그리라고 명령 하달
        RenderCommand::DrawIndexed(mesh->GetIndexBuffer().get());
    }

    // =================================================================
      // ★ 커스텀 쉐이더(기즈모 등) 전용 최적화 DrawMesh
      // =================================================================
    void Renderer3D::DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Shader>& shader, const DirectX::XMFLOAT4& color)
    {
        if (!mesh || !shader) return;

        // 1. 필요한 데이터(트랜스폼, 색상)만 채우고 나머지는 0/-1 처리
        TransformData transformData = {};
        transformData.Transform = CCEngine::Math::MathUtils::GetMatrixForShader(transform);
        transformData.BaseColor = color;
        transformData.EntityID = -1; // 피킹 무시

        s_Data->TransformConstantBuffer->SetData(&transformData, sizeof(TransformData));
        s_Data->TransformConstantBuffer->Bind(1);

        // ★ 쓸데없는 텍스처 바인딩(DefaultWhiteTexture->Bind) 완전히 제거됨!

        // 2. 쉐이더 및 메쉬 바인딩 후 드로우
        shader->Bind();
        shader->BindLayout(mesh->GetVertexBuffer()->GetLayout());
        mesh->Bind();

        RenderCommand::DrawIndexed(mesh->GetIndexBuffer().get());
    }

    //  뼈대가 있는 스킨드 메쉬 드로우 콜
    void Renderer3D::DrawSkinnedMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID, const std::vector<DirectX::XMMATRIX>& boneMatrices)
    {
        DrawSkinnedMesh(transform, mesh, texture, color, nullptr, entityID, boneMatrices);
    }

    void Renderer3D::DrawSkinnedMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, const MaterialAsset* material, int entityID, const std::vector<DirectX::XMMATRIX>& boneMatrices)
    {
        if (!mesh)
        {
            return;
        }

        // 1. 일반 트랜스폼 데이터 세팅
        TransformData transformData;
        transformData.Transform = CCEngine::Math::MathUtils::GetMatrixForShader(transform);
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
            boneData.BoneMatrices[i] = CCEngine::Math::MathUtils::GetMatrixForShader(boneMatrices[i]);
        }

        DirectX::XMMATRIX identityMatrix = DirectX::XMMatrixIdentity();
        for (size_t i = copyCount; i < 512; ++i)
        {
            boneData.BoneMatrices[i] = CCEngine::Math::MathUtils::GetMatrixForShader(identityMatrix);
        }

        // b2 (슬롯 2번)에 뼈대 상수 버퍼 바인딩
        s_Data->BoneConstantBuffer->SetData(&boneData, sizeof(BoneData));
        s_Data->BoneConstantBuffer->Bind(2);

        BindMaterialProperties(material, texture);

        std::shared_ptr<Shader> runtimeShader = material ? RuntimeShaderLibrary::GetShaderForMaterial(*material) : nullptr;
        Shader* activeShader = runtimeShader && runtimeShader->IsValid()
            ? runtimeShader.get()
            : s_Data->Base3DShader.get();

        activeShader->Bind();
        activeShader->BindLayout(mesh->GetVertexBuffer()->GetLayout());

        mesh->Bind();

        RenderCommand::DrawIndexed(mesh->GetIndexBuffer().get());
    }
}
