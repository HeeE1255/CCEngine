#pragma once
#include <string>
#include <cstddef>
#include "Core.h"
#include "Renderer/RendererHandle.h"

namespace CCEngine
{
    enum class TextureColorSpace { Linear, SRGB };
    enum class TextureMipContent { Color, Data, Normal };
    struct TextureCreateOptions
    {
        TextureColorSpace ColorSpace = TextureColorSpace::Linear;
        bool GenerateMips = false;
        TextureMipContent Content = TextureMipContent::Color;
        size_t MaxMipBytes = 256u * 1024u * 1024u;
    };
    struct TextureMipChain;
    struct TextureReloadRequest
    {
        std::string Path;
        TextureCreateOptions Options;
    };
    class CC_API Texture2D
    {
    public:
        virtual ~Texture2D() = default;
        TextureColorSpace GetColorSpace() const { return m_ColorSpace; }
        const std::string& GetSourcePath() const { return m_SourcePath; }
        uint32_t GetMipLevelCount() const { return m_MipLevelCount; }
        TextureCreateOptions GetCreateOptions() const
        {
            auto options = m_CreateOptions;
            options.ColorSpace = m_ColorSpace;
            return options;
        }
        static Texture2D* Create(const std::string& path, const TextureCreateOptions& options);
        static Texture2D* Create(uint32_t width, uint32_t height, const TextureCreateOptions& options);
        static Texture2D* Create(uint32_t width, uint32_t height, void* data, const TextureCreateOptions& options);

        virtual void Bind(uint32_t slot = 0) const = 0;
        virtual void Unbind(uint32_t slot = 0) const = 0;

        // 데이터 덮어쓰기 및 ImGui 출력을 위한 함수
        virtual void SetData(void* data, uint32_t size) = 0;
        virtual RendererHandle GetRendererID() const = 0;

        // 기존 파일 경로 로드 함수
        static Texture2D* Create(const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Linear);

        // 가로, 세로 크기만으로 빈 텍스처를 생성하는 함수
        static Texture2D* Create(uint32_t width, uint32_t height, TextureColorSpace colorSpace = TextureColorSpace::Linear);

		// CPU 메모리의 픽셀 데이터를 이용해 텍스처를 생성하는 함수 (예: 폰트 아틀라스)
        static Texture2D* Create(uint32_t width, uint32_t height, void* data, TextureColorSpace colorSpace = TextureColorSpace::Linear);
    protected:
        virtual void UploadMipChain(const TextureMipChain& chain);
        bool UpdateMipData(uint32_t width, uint32_t height, const void* pixels, size_t bytes);
        TextureCreateOptions m_CreateOptions;
        uint32_t m_MipLevelCount = 1;
        // sRGB는 RGB 샘플만 선형화한다. 알파와 SetData의 원본 바이트는 변환하지 않는다.
        explicit Texture2D(TextureColorSpace colorSpace = TextureColorSpace::Linear, const std::string& sourcePath = {})
            : m_ColorSpace(colorSpace), m_SourcePath(sourcePath) {}
        TextureColorSpace m_ColorSpace;
        std::string m_SourcePath;
    };
}
