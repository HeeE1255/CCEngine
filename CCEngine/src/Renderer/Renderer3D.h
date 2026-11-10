#pragma once
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Shader.h"
#include "Scene/Components.h" // MeshComponent::MeshType 사용을 위해 추가
#include <memory>
#include <functional>
#include <unordered_map>      // 맵 사용을 위해 추가

namespace CCEngine
{
    struct MaterialAsset;
    class Framebuffer;
    class Scene;

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

        // destination이 없으면 Player 백버퍼로 출력한다. Editor Game View도 같은 경로를 쓴다.
        static void RenderCamera(Scene& scene, const PerspectiveCamera& camera, const CameraComponent& settings,
            uint32_t width, uint32_t height, Framebuffer* destination = nullptr,
            const std::function<void()>& renderOverlay = {});
        static bool IsHDROutput();
        static bool ReloadMaterialTexture(const std::string& path);
        static bool PrepareReimportedMaterialResources(const std::vector<MaterialAsset>& materials,
            const std::vector<TextureReloadRequest>& directTextures = {},
            std::vector<std::shared_ptr<Texture2D>>* preparedDirectTextures = nullptr);

        // 기본 메시 그리기 (MeshComponent::MeshType이 Custom이 아닌 경우)
        static void DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID = -1);
        static void DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, const MaterialAsset* material, int entityID = -1, bool forceErrorShader = false);

		// 기즈모 등에서 텍스처 없이 단색으로 그릴 때 사용하는 오버로드 함수
        static void DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Shader>& shader, const DirectX::XMFLOAT4& color);
        // 뼈대 애니메이션이 있는 스킨드 메쉬 드로우 콜
        static void DrawSkinnedMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID, const std::vector<DirectX::XMMATRIX>& boneMatrices);
        static void DrawSkinnedMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, const MaterialAsset* material, int entityID, const std::vector<DirectX::XMMATRIX>& boneMatrices, bool forceErrorShader = false);
    private:
        static std::shared_ptr<Texture2D> GetCachedMaterialTexture(const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Linear,
            bool generateMips = false, TextureMipContent content = TextureMipContent::Color);
        static void BindMaterialProperties(const MaterialAsset* material, const std::shared_ptr<Texture2D>& fallbackAlbedo, const Shader& shader);

        struct RenderData
        {
            std::shared_ptr<Shader> Base3DShader;
            std::shared_ptr<ConstantBuffer> CameraConstantBuffer;    // View, Proj 행렬용
            std::shared_ptr<ConstantBuffer> TransformConstantBuffer; // Transform 행렬 및 색상용
            std::unordered_map<MeshComponent::MeshType, std::shared_ptr<Mesh>> DefaultMeshes; // 기본 도형들을 저장하는 맵 (MeshType -> Mesh)
            std::shared_ptr<Texture2D> DefaultWhiteTexture;// 텍스처가 없는 경우 사용할 기본 흰색 텍스처
            std::shared_ptr<ConstantBuffer> BoneConstantBuffer; // 뼈대 애니메이션용 상수 버퍼
            std::shared_ptr<ConstantBuffer> SceneConstantBuffer; // (조명) 버퍼
            std::shared_ptr<ConstantBuffer> MaterialPropertyConstantBuffer; // 커스텀 Material 값용. BoneBuffer와 겹치지 않도록 b4를 쓴다.
            std::shared_ptr<ConstantBuffer> MaterialUVConstantBuffer;
            struct CachedTexture
            {
                std::string Path;
                TextureCreateOptions Options;
                std::shared_ptr<Texture2D> Texture;
            };
            std::unordered_map<std::string, CachedTexture> MaterialPropertyTextureCache;
            std::unique_ptr<Framebuffer> HDRTarget;
            std::shared_ptr<Shader> ToneMapShader;
            std::shared_ptr<VertexBuffer> ScreenVertices;
            std::shared_ptr<IndexBuffer> ScreenIndices;
            std::shared_ptr<ConstantBuffer> ToneMapConstants;
            bool HDROutput = false;
        };

        static RenderData* s_Data;
    };
}
