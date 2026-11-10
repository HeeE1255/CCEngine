#pragma once
#include "Core.h"
#include "Renderer/Buffer.h"
#include "Renderer/RendererHandle.h"
#include "Renderer/SamplerDescription.h"
#include <string>

namespace CCEngine
{
    class CC_API RendererAPI
    {
    public:
        // 우리가 지원할 그래픽스 API 종류
        enum class API
        {
            None = 0, DirectX11 = 1, Vulkan = 2, OpenGL = 3, DirectX12 = 4
        };

        enum class BlendMode
        {
            Opaque = 0, Transparent, MRTPicking
        };

        enum class CullMode
        {
            Back = 0, None
        };

        virtual ~RendererAPI() = default;
        // 1은 anisotropy 미지원이다. 설정을 조용히 낮추지 않고 호출자가 지원 범위를 판단한다.
        virtual uint32_t GetMaxSamplerAnisotropy() const { return 1; }

        // 렌더러가 무조건 구현해야 하는 필수 기능들 (순수 가상 함수)
        virtual void SetClearColor(float r, float g, float b, float a) = 0;
        virtual void Clear() = 0;
        virtual void DrawIndexed(IndexBuffer* indexBuffer) = 0;
        virtual void DrawIndexed(uint32_t indexCount) = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void ResizeContext(uint32_t width, uint32_t height) = 0;
        virtual void SetDepthTest(bool enable) = 0;
        virtual void SetDepthWrite(bool enable) = 0;
        // Y축 뒤집힘 여부 반환 (OpenGL은 true, DirectX는 false)
        virtual bool IsYAxisFlipped() const = 0;

        virtual void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetScissorEnable(bool enable) = 0;
        virtual void SetBlendMode(BlendMode mode) = 0;
        virtual void SetCullMode(CullMode mode) = 0;
        virtual void BindTexture(uint32_t slot, RendererHandle rendererID) = 0;
        virtual void UnbindTextures(uint32_t startSlot, uint32_t count) = 0;
        // s0만 변경한다. nullptr는 기존 기본 샘플링으로 복귀하며 s1(point)는 유지한다.
        virtual void SetCommonSampler(const SamplerDescription* description) = 0;
        // Material slot 0..3 -> HLSL s2..s5. s0와 정수 ID용 s1은 기존 계약을 유지한다.
        virtual void SetMaterialSampler(uint32_t slot, const SamplerDescription* description) = 0;

        inline static API GetAPI() { return s_API; }
        static void SetAPI(API api);
        static const char* GetAPIName(API api);
        static API ParseAPIName(const std::string& name);
        static const char* GetAPIArgument(API api);
        static const char* GetUnavailableReason(API api);
        static void RequireAvailable(API api);

    private:
        static API s_API;
    };
}
