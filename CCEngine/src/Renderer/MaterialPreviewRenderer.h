#pragma once

#include "Renderer/Framebuffer.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/Mesh.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/RendererAPI.h"
#include <DirectXMath.h>
#include <algorithm>
#include <memory>

namespace CCEngine
{
    struct MaterialPreviewRenderOptions
    {
        float Yaw = 0.55f;
        float Pitch = -0.20f;
        uint32_t TargetWidth = 0;
        uint32_t TargetHeight = 0;
        uint32_t RestoreViewportWidth = 0;
        uint32_t RestoreViewportHeight = 0;
    };

    inline void RenderMaterialPreviewToFramebuffer(Framebuffer* framebuffer, const std::shared_ptr<Mesh>& previewMesh, const MaterialAsset& material, const MaterialPreviewRenderOptions& options)
    {
        if (!framebuffer || !previewMesh)
            return;

        const FramebufferSpecification& spec = framebuffer->GetSpecification();
        const uint32_t targetWidth = options.TargetWidth > 0 ? options.TargetWidth : spec.Width;
        const uint32_t targetHeight = options.TargetHeight > 0 ? options.TargetHeight : spec.Height;

        framebuffer->Bind();
        if (targetWidth > 0 && targetHeight > 0)
            RenderCommand::SetViewport(0, 0, targetWidth, targetHeight);

        RenderCommand::SetScissorEnable(false);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Opaque);
        RenderCommand::SetCullMode(RendererAPI::CullMode::Back);
        RenderCommand::SetDepthTest(true);
        Renderer::SetClearColor(0.10f, 0.10f, 0.11f, 1.0f);
        Renderer::Clear();

        PerspectiveCamera camera(35.0f, 1.0f, 0.1f, 20.0f);
        camera.SetPosition({ 0.0f, 0.0f, -3.0f });

        SceneLightData lightData;
        lightData.LightCount = 2;
        lightData.Lights[0].Direction = { -0.45f, -0.75f, 0.35f };
        lightData.Lights[0].Color = { 1.0f, 0.96f, 0.90f };
        lightData.Lights[0].Intensity = 1.15f;
        lightData.Lights[1].Direction = { 0.80f, 0.35f, 0.20f };
        lightData.Lights[1].Color = { 0.55f, 0.65f, 1.0f };
        lightData.Lights[1].Intensity = 0.35f;

        Renderer3D::BeginScene(camera, lightData);

        // 같은 프리뷰 렌더러를 인스펙터와 에셋 브라우저가 함께 쓴다.
        // 한쪽만 렌더 상태를 다르게 복구해서 썸네일이 비는 문제를 막기 위한 공통 경로다.
        DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationRollPitchYaw(options.Pitch, options.Yaw, 0.0f);
        // 프리뷰도 실제 씬 렌더와 같은 재질 입력값을 써야 한다.
        // 여기서 색을 더하거나 밝게 보정하면 인스펙터, 썸네일, 씬 결과가 서로 달라진다.
        DirectX::XMFLOAT4 previewColor = material.AlbedoColor;
        previewColor.w = 1.0f;
        Renderer3D::DrawMesh(rotation, previewMesh, material.AlbedoTexture, previewColor, -1);

        Renderer3D::EndScene();
        framebuffer->Unbind();

        if (options.RestoreViewportWidth > 0 && options.RestoreViewportHeight > 0)
            RenderCommand::SetViewport(0, 0, options.RestoreViewportWidth, options.RestoreViewportHeight);
    }
}
