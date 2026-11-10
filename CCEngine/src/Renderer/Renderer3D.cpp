#include "Renderer3D.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/MaterialTextureBinding.h"
#include "Renderer/MaterialPropertyBinding.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/RenderCommand.h" // DX11 RHI의 RenderCommand 호출용!
#include "Renderer/RuntimeShaderLibrary.h"
#include "Utils/MathUtils.h"
#include "Renderer/Framebuffer.h"
#include "Scene/Scene.h"
#include "Core/ConsoleLog.h"
#include "stb_image.h"
#include <cmath>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <stdexcept>

namespace CCEngine
{
    // HLSL의 b0 레지스터와 매칭 (카메라)
    struct CameraData
    {
        DirectX::XMMATRIX ViewProjection;
        DirectX::XMFLOAT3 CameraPosition = { 0.0f, 0.0f, 0.0f };
        float Padding = 0.0f;
    };

    struct SceneBufferData
    {
        LightInfo Lights[4];
        int LightCount;
        float Padding[3];
    };

    constexpr uint32_t MaxMaterialColorProperties = 8;
    constexpr uint32_t MaxMaterialScalarSlots = 16;
    struct MaterialUVBufferData
    {
        DirectX::XMFLOAT4 Rows[8];
    };
    static_assert(sizeof(MaterialUVBufferData) == 128);

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
    struct BoneData // 32768 바이트 (64 bytes * 512 matrices)
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
        DirectX::XMFLOAT4 SurfaceValues = { 0.5f, 0.0f, 0.0f, 0.0f }; // x: Roughness, y: Metallic, w: NormalTexture 사용 여부
    };

    // 오프라인 reflection 검사의 b4 계약과 CPU 업로드 배치를 함께 고정한다.
    // 필드 추가/순서 변경 시 셰이더 생성기와 검증 규칙도 같이 변경해야 한다.
    static_assert(sizeof(CameraData) == 80);
    static_assert(offsetof(CameraData, CameraPosition) == 64);
    static_assert(offsetof(CameraData, Padding) == 76);
    static_assert(sizeof(TransformData) == 96);
    static_assert(offsetof(TransformData, BaseColor) == 64);
    static_assert(offsetof(TransformData, EntityID) == 80);
    static_assert(offsetof(TransformData, HasAnimation) == 84);
    static_assert(offsetof(TransformData, padding) == 88);
    static_assert(sizeof(BoneData) == 32768);
    static_assert(sizeof(LightInfo) == 32);
    static_assert(offsetof(LightInfo, Intensity) == 12);
    static_assert(offsetof(LightInfo, Color) == 16);
    static_assert(offsetof(LightInfo, Padding) == 28);
    static_assert(sizeof(SceneBufferData) == 144);
    static_assert(offsetof(SceneBufferData, LightCount) == 128);
    static_assert(offsetof(SceneBufferData, Padding) == 132);
    static_assert(sizeof(MaterialPropertyBufferData) == 288);
    static_assert(offsetof(MaterialPropertyBufferData, AlbedoColor) == 0);
    static_assert(offsetof(MaterialPropertyBufferData, ColorProperties) == 16);
    static_assert(offsetof(MaterialPropertyBufferData, ScalarProperties) == 144);
    static_assert(offsetof(MaterialPropertyBufferData, ToggleProperties) == 208);
    static_assert(offsetof(MaterialPropertyBufferData, SurfaceValues) == 272);

    Renderer3D::RenderData* Renderer3D::s_Data = new Renderer3D::RenderData();

    void Renderer3D::Init()
    {
        if (!s_Data)
            s_Data = new RenderData();
        s_Data->Base3DShader.reset(Shader::Create("assets/shaders/Base3D.hlsl"));
        if (!s_Data->Base3DShader || !s_Data->Base3DShader->IsValid())
            throw std::runtime_error("Renderer3D shader initialization failed: assets/shaders/Base3D.hlsl");

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
        BoneData initialBones;
        for (auto& matrix : initialBones.BoneMatrices)
            matrix = CCEngine::Math::MathUtils::GetMatrixForShader(DirectX::XMMatrixIdentity());
        s_Data->BoneConstantBuffer->SetData(&initialBones, sizeof(initialBones));
        s_Data->SceneConstantBuffer.reset(ConstantBuffer::Create(sizeof(SceneBufferData)));
        s_Data->MaterialPropertyConstantBuffer.reset(ConstantBuffer::Create(sizeof(MaterialPropertyBufferData)));
        s_Data->MaterialUVConstantBuffer.reset(ConstantBuffer::Create(sizeof(MaterialUVBufferData)));
    }

    void Renderer3D::Shutdown()
    {
        // 스마트 포인터를 사용했으므로 s_Data를 지우면 리소스가 자동 해제
        delete s_Data;
        s_Data = nullptr;
    }

    void Renderer3D::BeginScene(const PerspectiveCamera& camera, const SceneLightData& lightData)
    {
        // 렌더 패스는 이전 패스가 남긴 GPU 상태에 의존하면 안 된다.
        // Player는 2D 패스 뒤에 3D를 그리므로 상태를 복구하지 않으면 MRTPicking 블렌드와
        // 양면 렌더링이 남아 곡면 메시에서 겹침/음영 깨짐이 나타날 수 있다.
        RenderCommand::ApplyPassState({
            RendererAPI::BlendMode::Opaque,
            RendererAPI::CullMode::Back,
            true,
            false });

        // 매 프레임 시작 시, 카메라 행렬(ViewProjection)을 계산하여 상수 버퍼에 세팅!
        CameraData camData;
		camData.ViewProjection = CCEngine::Math::MathUtils::GetMatrixForShader(camera.GetViewProjectionMatrix());
        camData.CameraPosition = camera.GetPosition();
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
        // 다음 오버레이/카메라 패스가 투명 재질의 read-only depth 상태를 물려받지 않게 복구한다.
        RenderCommand::ApplyPassState({ RendererAPI::BlendMode::Opaque, RendererAPI::CullMode::Back, true, false, true });
    }

    bool Renderer3D::IsHDROutput() { return s_Data && s_Data->HDROutput; }

    void Renderer3D::RenderCamera(Scene& scene, const PerspectiveCamera& camera, const CameraComponent& settings,
        uint32_t width, uint32_t height, Framebuffer* destination, const std::function<void()>& renderOverlay)
    {
        if (!width || !height) return;
        if (!settings.HDR)
        {
            scene.OnRender2D(camera);
            scene.OnRender3D(camera);
            if (renderOverlay) renderOverlay();
            return;
        }
        if (!s_Data->ToneMapShader)
        {
            auto shader = std::shared_ptr<Shader>(Shader::Create("assets/shaders/ToneMap.hlsl"));
            if (!shader || !shader->IsValid()) throw std::runtime_error("HDR tone mapping shader is unavailable");
            // 화면 전체를 덮는 삼각형은 대각선 이음새와 중복 픽셀 실행을 없앤다.
            float vertices[] = { -1, -1, 0, 3, -1, 0, -1, 3, 0 };
            uint32_t indices[] = { 0, 1, 2 };
            auto vb = std::shared_ptr<VertexBuffer>(VertexBuffer::Create(vertices, sizeof(vertices)));
            vb->SetLayout({ { ShaderDataType::Float3, "POSITION" } });
            auto ib = std::shared_ptr<IndexBuffer>(IndexBuffer::Create(indices, 3));
            auto constants = std::shared_ptr<ConstantBuffer>(ConstantBuffer::Create(16));
            s_Data->ScreenVertices = std::move(vb); s_Data->ScreenIndices = std::move(ib);
            s_Data->ToneMapConstants = std::move(constants); s_Data->ToneMapShader = std::move(shader);
        }
        if (!s_Data->HDRTarget)
            s_Data->HDRTarget.reset(Framebuffer::Create({ width, height, FramebufferColorFormat::RGBA16Float }));
        else if (s_Data->HDRTarget->GetSpecification().Width != width || s_Data->HDRTarget->GetSpecification().Height != height)
            s_Data->HDRTarget->Resize(width, height);

        s_Data->HDRTarget->Bind();
        RenderCommand::SetClearColor(0, 0, 0, 1);
        RenderCommand::Clear();
        s_Data->HDROutput = true;
        try
        {
            scene.OnRender2D(camera);
            scene.OnRender3D(camera);
            if (renderOverlay) renderOverlay();
        }
        catch (...)
        {
            s_Data->HDROutput = false;
            s_Data->HDRTarget->Unbind();
            throw;
        }
        s_Data->HDROutput = false;
        s_Data->HDRTarget->Unbind();
        if (destination) destination->Bind();
        RenderCommand::SetViewport(0, 0, width, height);
        RenderCommand::ApplyPassState({ RendererAPI::BlendMode::Opaque, RendererAPI::CullMode::None, false, false });
        const float exposure = std::isfinite(settings.ExposureEV) ? std::clamp(settings.ExposureEV, -16.0f, 16.0f) : 0.0f;
        const float values[4] = { std::exp2(exposure), settings.ToneMapping == 1 ? 1.0f : 0.0f,
            RendererAPI::GetAPI() == RendererAPI::API::OpenGL ? 1.0f : 0.0f, 0.0f };
        s_Data->ToneMapConstants->SetData(values, sizeof(values));
        s_Data->ToneMapConstants->Bind(0);
        s_Data->ToneMapShader->Bind();
        s_Data->ToneMapShader->BindLayout(s_Data->ScreenVertices->GetLayout());
        s_Data->ScreenVertices->Bind(); s_Data->ScreenIndices->Bind();
        RenderCommand::BindTexture(0, s_Data->HDRTarget->GetColorAttachmentRendererID());
        RenderCommand::DrawIndexed(s_Data->ScreenIndices.get());
        RenderCommand::UnbindTextures(0, 1);
        if (destination)
        {
            // 최종 LDR 색상은 톤매핑 결과를 쓰되, 선택 ID는 HDR 장면 패스의 값을 보존한다.
            destination->Unbind();
            if (!s_Data->HDRTarget->CopyAttachmentTo(1, *destination))
                throw std::runtime_error("HDR picking attachment copy failed");
        }
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

    std::shared_ptr<Texture2D> Renderer3D::GetCachedMaterialTexture(const std::string& path, TextureColorSpace colorSpace,
        bool generateMips, TextureMipContent content)
    {
        if (path.empty())
            return nullptr;

        std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path);
        // 같은 파일도 색상 슬롯과 수치 슬롯은 GPU 샘플 해석이 다르므로 별도로 캐시한다.
        std::string key = normalizedPath.string() + (colorSpace == TextureColorSpace::SRGB ? "|srgb" : "|linear");
        // 같은 파일도 법선 재정규화와 색/데이터 축소 결과가 다르므로 GPU 텍스처를 공유하면 안 된다.
        key += "|mip=" + std::to_string(generateMips) + "|content=" + std::to_string(static_cast<int>(content));
        auto it = s_Data->MaterialPropertyTextureCache.find(key);
        if (it != s_Data->MaterialPropertyTextureCache.end())
            return it->second.Texture;
        if (!std::filesystem::exists(path)) return nullptr;

        std::shared_ptr<Texture2D> texture;
        TextureCreateOptions options;
        options.ColorSpace = colorSpace; options.GenerateMips = generateMips; options.Content = content;
        texture.reset(Texture2D::Create(normalizedPath.string(), options));
        s_Data->MaterialPropertyTextureCache[key] = { normalizedPath.string(), options, texture };
        return texture;
    }

    bool Renderer3D::ReloadMaterialTexture(const std::string& path)
    {
        if (!s_Data || path.empty()) return true;
        try
        {
            std::vector<std::pair<RenderData::CachedTexture*, std::shared_ptr<Texture2D>>> replacements;
            for (auto& [key, cached] : s_Data->MaterialPropertyTextureCache)
            {
                std::error_code error;
                if (!std::filesystem::equivalent(cached.Path, path, error)) continue;
                std::shared_ptr<Texture2D> fresh(Texture2D::Create(cached.Path, cached.Options));
                if (!fresh) throw std::runtime_error("Texture creation returned no resource");
                replacements.emplace_back(&cached, std::move(fresh));
            }
            // 색 공간과 mip 정책이 다른 모든 변형을 먼저 준비한다. 하나라도 실패하면 기존 GPU 자원을 유지한다.
            for (auto& [cached, fresh] : replacements) cached->Texture = std::move(fresh);
            return true;
        }
        catch (const std::exception& error)
        {
            ConsoleLog::Error("Texture reload preserved previous resources: " + path + ": " + error.what());
            return false;
        }
    }

    bool Renderer3D::PrepareReimportedMaterialResources(const std::vector<MaterialAsset>& materials,
        const std::vector<TextureReloadRequest>& directTextures,
        std::vector<std::shared_ptr<Texture2D>>* preparedDirectTextures)
    {
        if (preparedDirectTextures) preparedDirectTextures->clear();
        if (!s_Data) return false;
        try
        {
            // shared_ptr 복사만 먼저 수행한다. 실패 시 원래 맵과 기존 GPU 자원은 전혀 변경하지 않는다.
            auto staged = s_Data->MaterialPropertyTextureCache;
            std::unordered_map<std::string, RenderData::CachedTexture> requests;
            const auto request = [&](const std::string& path, const TextureCreateOptions& options) -> std::string
            {
                if (path.empty()) return {};
                const auto normalized = std::filesystem::weakly_canonical(path).string();
                if (!std::filesystem::is_regular_file(normalized))
                    throw std::runtime_error("Texture file missing: " + normalized);
                const std::string key = normalized + (options.ColorSpace == TextureColorSpace::SRGB ? "|srgb" : "|linear") +
                    "|mip=" + std::to_string(options.GenerateMips) + "|content=" + std::to_string(static_cast<int>(options.Content));
                requests[key] = { normalized, options, nullptr };
                // 이미 사용 중인 다른 shader/색 공간/mip 변형도 같은 원본 버전으로 맞춘다.
                for (const auto& [cachedKey, cached] : s_Data->MaterialPropertyTextureCache)
                {
                    std::error_code error;
                    if (std::filesystem::equivalent(cached.Path, normalized, error))
                        requests[cachedKey] = { cached.Path, cached.Options, nullptr };
                }
                return key;
            };
            std::vector<std::string> directKeys;
            directKeys.reserve(directTextures.size());
            for (const auto& direct : directTextures)
            {
                if (direct.Path.empty()) throw std::runtime_error("Direct texture reload requires a file path");
                directKeys.push_back(request(direct.Path, direct.Options));
            }
            for (const auto& material : materials)
            {
                auto runtimeShader = RuntimeShaderLibrary::GetShaderForMaterial(material);
                const Shader* shader = runtimeShader && runtimeShader->IsValid() ? runtimeShader.get() : s_Data->Base3DShader.get();
                if (!shader || !shader->IsValid()) throw std::runtime_error("Material shader is unavailable");
                for (const auto& [resource, slot] : shader->GetTextureBindings())
                {
                    const bool albedo = resource == "AlbedoTexture" || resource == "g_AlbedoMap";
                    const bool normal = resource == "NormalTexture" || resource == "g_NormalMap";
                    const std::string property = albedo ? "AlbedoTexture" : normal ? "NormalTexture" : resource;
                    const bool color = albedo || std::any_of(shader->GetMaterialProperties().begin(), shader->GetMaterialProperties().end(),
                        [&](const ShaderPropertyDefinition& definition) { return definition.Name == property && definition.TextureSRGB; });
                    const int semantic = albedo ? 0 : normal ? 1 : resource == "MetallicTexture" ? 2 : resource == "RoughnessTexture" ? 3 : -1;
                    auto sampler = material.CommonSampler.value_or(SamplerDescription{});
                    if (semantic >= 0 && shader->UsesMaterialSamplers() && material.MaterialSamplers[semantic])
                        sampler = *material.MaterialSamplers[semantic];
                    TextureCreateOptions options;
                    options.GenerateMips = sampler.Mip != SamplerMipFilter::Disabled;
                    options.Content = normal ? TextureMipContent::Normal : color ? TextureMipContent::Color : TextureMipContent::Data;
                    std::string path;
                    auto value = material.ShaderProperties.find(property);
                    if (value != material.ShaderProperties.end() && value->second.Type == ShaderPropertyType::Texture2D)
                        path = value->second.TexturePath;
                    if (path.empty() && albedo) path = material.AlbedoTexturePath;
                    if (path.empty() && normal) path = material.NormalTexturePath;
                    request(path, options);
                    // Scene/Preview와 HDR Game View의 입력 색 공간이 다를 수 있어 양쪽을 준비한다.
                    if (color && !normal)
                    {
                        options.ColorSpace = TextureColorSpace::SRGB;
                        request(path, options);
                    }
                }
            }
            for (auto& [key, entry] : requests)
            {
                int width = 0, height = 0, channels = 0;
                if (!stbi_info(entry.Path.c_str(), &width, &height, &channels) ||
                    width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
                    uint64_t(width) * uint64_t(height) * 4 > entry.Options.MaxMipBytes)
                    throw std::runtime_error("Invalid texture or decoded image budget exceeded: " + entry.Path);
                entry.Texture.reset(Texture2D::Create(entry.Path, entry.Options));
                if (!entry.Texture || !entry.Texture->GetRendererID())
                    throw std::runtime_error("GPU texture creation failed: " + entry.Path);
                staged[key] = std::move(entry);
            }
            std::vector<std::shared_ptr<Texture2D>> prepared;
            prepared.reserve(directKeys.size());
            for (const auto& key : directKeys) prepared.push_back(staged.at(key).Texture);
            // 직접 보유한 Mesh 텍스처도 같은 준비 단계에 포함한다. 결과 배열 할당 실패까지 기존 캐시를 유지한다.
            s_Data->MaterialPropertyTextureCache.swap(staged);
            if (preparedDirectTextures) preparedDirectTextures->swap(prepared);
            return true;
        }
        catch (const std::exception& error)
        {
            ConsoleLog::Error("Material resource update cancelled; previous resources preserved: " + std::string(error.what()));
            return false;
        }
    }

    void Renderer3D::BindMaterialProperties(const MaterialAsset* material, const std::shared_ptr<Texture2D>& fallbackAlbedo, const Shader& shader)
    {
        MaterialPropertyBufferData buffer;
        const MaterialAsset* source = material;
        // 이 계약을 선언한 셰이더에서만 b6를 사용한다. 기존 사용자 셰이더의 b6는 건드리지 않는다.
        if (shader.UsesMaterialUVBuffer())
        {
            MaterialUVBufferData uv;
            for (size_t i = 0; i < 4; ++i)
            {
                const auto transform = source ? source->UVTransforms[i] : MaterialUVTransform{};
                if (!transform.GetRows(uv.Rows[i * 2], uv.Rows[i * 2 + 1]))
                    MaterialUVTransform{}.GetRows(uv.Rows[i * 2], uv.Rows[i * 2 + 1]);
            }
            s_Data->MaterialUVConstantBuffer->SetData(&uv, sizeof(uv));
            s_Data->MaterialUVConstantBuffer->Bind(6);
        }
        RenderCommand::SetCommonSampler(source && source->CommonSampler ? &*source->CommonSampler : nullptr);
        for (uint32_t slot = 0; slot < 4; ++slot)
        {
            // 새 계약이 없는 셰이더에는 전용 슬롯을 적용하지 않는다. 슬롯 설정이 없는 구버전
            // 재질은 공통 sampler를 상속하고, 그것도 없으면 기존 linear/clamp로 유지한다.
            const SamplerDescription fallback{};
            const SamplerDescription* sampler = nullptr;
            if (shader.UsesMaterialSamplers())
                sampler = source && source->MaterialSamplers[slot] ? &*source->MaterialSamplers[slot] :
                    source && source->CommonSampler ? &*source->CommonSampler : &fallback;
            RenderCommand::SetMaterialSampler(slot, sampler);
        }
        const bool supportsLinearColor = std::any_of(shader.GetMaterialProperties().begin(), shader.GetMaterialProperties().end(),
            [](const ShaderPropertyDefinition& property) { return property.Name == "LinearColorSpace" && property.Type == ShaderPropertyType::Toggle; });
        bool linearColor = false;
        if (source && supportsLinearColor)
        {
            auto option = source->ShaderProperties.find("LinearColorSpace");
            linearColor = option != source->ShaderProperties.end() && option->second.Type == ShaderPropertyType::Toggle && option->second.BoolValue;
        }
        // HDR에서는 조명과 혼합을 선형 공간에서 끝낸 후 최종 패스가 출력 변환을 맡는다.
        if (s_Data->HDROutput) linearColor = true;
        // b4의 크기와 Time용 z를 유지한다. w의 bit 0=노멀맵, bit 1=선형 입력, bit 2=HDR 출력이다.
        const float outputFlags = (linearColor ? 2.0f : 0.0f) + (s_Data->HDROutput ? 4.0f : 0.0f);
        buffer.SurfaceValues.w = outputFlags;
        if (source)
        {
            buffer.AlbedoColor = source->AlbedoColor;
            buffer.SurfaceValues.x = source->Roughness;
            buffer.SurfaceValues.y = source->Metallic;
        }

        uint32_t colorIndex = 0;
        uint32_t scalarIndex = 0;
        uint32_t toggleIndex = 0;

        if (source)
        {
            // 기본 표면 값은 기존 머티리얼 필드를 유지한다. 사용자 속성만 선언 순서로 배치한다.
            const auto reserved = [&](const char* name, ShaderPropertyType type) -> const ShaderPropertyValue* {
                auto it = source->ShaderProperties.find(name);
                return it != source->ShaderProperties.end() && it->second.Type == type ? &it->second : nullptr;
            };
            if (const auto* v = reserved("AlbedoColor", ShaderPropertyType::Color)) buffer.AlbedoColor = v->Color;
            if (const auto* v = reserved("Roughness", ShaderPropertyType::Float)) buffer.SurfaceValues.x = v->FloatValue;
            if (const auto* v = reserved("Metallic", ShaderPropertyType::Float)) buffer.SurfaceValues.y = v->FloatValue;
            VisitMaterialProperties(shader.GetMaterialProperties(), source->ShaderProperties,
                [&](const std::string& name, const ShaderPropertyValue& value)
            {
                if (value.Type == ShaderPropertyType::Color)
                {
                    if (name == "AlbedoColor")
                        return;
                    else if (colorIndex < MaxMaterialColorProperties)
                        buffer.ColorProperties[colorIndex++] = value.Color;
                }
                else if (value.Type == ShaderPropertyType::Float)
                {
                    if (name == "Roughness")
                        return;
                    else if (name == "Metallic")
                        return;
                    else
                        WriteScalar(buffer, scalarIndex++, value.FloatValue);
                }
                else if (value.Type == ShaderPropertyType::Toggle)
                {
                    WriteToggle(buffer, toggleIndex++, value.BoolValue);
                }
            });
        }

        // 컴파일된 셰이더의 슬롯을 기준으로 채운다. 머티리얼의 저장 순서나 빈 값은 슬롯에 영향을 주지 않는다.
        BindMaterialTextures(shader, s_Data->DefaultWhiteTexture, [&](const std::string& resource)
        {
            const bool albedo = resource == "AlbedoTexture" || resource == "g_AlbedoMap";
            const bool normal = resource == "NormalTexture" || resource == "g_NormalMap";
            const std::string property = albedo ? "AlbedoTexture" : normal ? "NormalTexture" : resource;
            const bool declaredSRGB = std::any_of(shader.GetMaterialProperties().begin(), shader.GetMaterialProperties().end(),
                [&](const ShaderPropertyDefinition& definition) { return definition.Name == property && definition.TextureSRGB; });
            const auto colorSpace = !normal && linearColor && (albedo || declaredSRGB) ? TextureColorSpace::SRGB : TextureColorSpace::Linear;
            const int semantic = albedo ? 0 : normal ? 1 : resource == "MetallicTexture" ? 2 : resource == "RoughnessTexture" ? 3 : -1;
            SamplerDescription sampling;
            if (source)
            {
                sampling = source->CommonSampler.value_or(SamplerDescription{});
                if (semantic >= 0 && shader.UsesMaterialSamplers() && source->MaterialSamplers[semantic])
                    sampling = *source->MaterialSamplers[semantic];
            }
            const bool generateMips = sampling.Mip != SamplerMipFilter::Disabled;
            const auto content = normal ? TextureMipContent::Normal :
                (albedo || declaredSRGB) ? TextureMipContent::Color : TextureMipContent::Data;
            const auto loadTexture = [&](const std::string& path) {
                return GetCachedMaterialTexture(path, colorSpace, generateMips, content);
            };
            std::shared_ptr<Texture2D> texture;
            bool fallbackTexture = false;
            if (source)
            {
                auto it = source->ShaderProperties.find(property);
                if (it != source->ShaderProperties.end() && it->second.Type == ShaderPropertyType::Texture2D)
                    texture = loadTexture(it->second.TexturePath);
                if (!texture && albedo) texture = loadTexture(source->AlbedoTexturePath);
                if (!texture && albedo) { texture = source->AlbedoTexture; fallbackTexture = true; }
                if (!texture && normal) texture = loadTexture(source->NormalTexturePath);
            }
            if (!texture && albedo) { texture = fallbackAlbedo; fallbackTexture = true; }
            if (fallbackTexture && texture && !texture->GetSourcePath().empty())
            {
                if (auto replacement = loadTexture(texture->GetSourcePath())) texture = std::move(replacement);
            }
            if (normal && texture) buffer.SurfaceValues.w = outputFlags + 1.0f;
            return texture;
        });

        s_Data->MaterialPropertyConstantBuffer->SetData(&buffer, sizeof(MaterialPropertyBufferData));
        s_Data->MaterialPropertyConstantBuffer->Bind(4);

    }

    // 일반 정적 메쉬 드로우 콜
    void Renderer3D::DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, int entityID)
    {
        DrawMesh(transform, mesh, texture, color, nullptr, entityID);
    }

    void Renderer3D::DrawMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, const MaterialAsset* material, int entityID, bool forceErrorShader)
    {
        if (!mesh)
        {
            return;
        }

        const bool transparent = material && material->IsTransparent();
        RenderCommand::ApplyPassState({
            transparent ? RendererAPI::BlendMode::Transparent : RendererAPI::BlendMode::Opaque,
            RendererAPI::CullMode::Back, true, false, !transparent });

        // 1. 현재 그릴 오브젝트의 데이터를 세팅
        TransformData transformData;
        transformData.Transform = CCEngine::Math::MathUtils::GetMatrixForShader(transform);
        transformData.BaseColor = color;
        transformData.EntityID = entityID;
        transformData.HasAnimation = 0;

        // 정적 메시도 스키닝과 같은 셰이더를 쓴다. 분기가 꺼져 있어도 활성 uniform
        // 블록에는 유효한 버퍼가 필요하므로 이전 draw의 바인딩에 의존하지 않는다.
        s_Data->BoneConstantBuffer->Bind(2);

        // b1 (슬롯 1번)에 상수 버퍼 데이터 덮어쓰고 바인딩
        s_Data->TransformConstantBuffer->SetData(&transformData, sizeof(TransformData));
        s_Data->TransformConstantBuffer->Bind(1);

        // Missing Material은 None과 다르다. None은 기본 셰이더로 그리고, Missing은 에러 셰이더로 보여준다.
        // 이렇게 해야 깨진 참조가 씬 뷰에서 바로 드러난다.
        std::shared_ptr<Shader> runtimeShader = forceErrorShader
            ? RuntimeShaderLibrary::GetErrorShader()
            : (material ? RuntimeShaderLibrary::GetShaderForMaterial(*material) : nullptr);
        Shader* activeShader = runtimeShader && runtimeShader->IsValid()
            ? runtimeShader.get()
            : s_Data->Base3DShader.get();
        BindMaterialProperties(material, texture, *activeShader);

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

    void Renderer3D::DrawSkinnedMesh(const DirectX::XMMATRIX& transform, const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Texture2D>& texture, const DirectX::XMFLOAT4& color, const MaterialAsset* material, int entityID, const std::vector<DirectX::XMMATRIX>& boneMatrices, bool forceErrorShader)
    {
        if (!mesh)
        {
            return;
        }

        const bool transparent = material && material->IsTransparent();
        RenderCommand::ApplyPassState({
            transparent ? RendererAPI::BlendMode::Transparent : RendererAPI::BlendMode::Opaque,
            RendererAPI::CullMode::Back, true, false, !transparent });

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

        std::shared_ptr<Shader> runtimeShader = forceErrorShader
            ? RuntimeShaderLibrary::GetErrorShader()
            : (material ? RuntimeShaderLibrary::GetShaderForMaterial(*material) : nullptr);
        Shader* activeShader = runtimeShader && runtimeShader->IsValid()
            ? runtimeShader.get()
            : s_Data->Base3DShader.get();
        BindMaterialProperties(material, texture, *activeShader);

        activeShader->Bind();
        activeShader->BindLayout(mesh->GetVertexBuffer()->GetLayout());

        mesh->Bind();

        RenderCommand::DrawIndexed(mesh->GetIndexBuffer().get());
    }
}
