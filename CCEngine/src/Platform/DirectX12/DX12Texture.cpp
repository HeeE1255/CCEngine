#include "DX12Texture.h"
#if defined(CC_ENABLE_DX12_RHI)
#include "stb_image.h"
#include <stdexcept>
namespace CCEngine
{
    namespace
    {
        uint32_t PixelBytes(uint32_t width, uint32_t height)
        {
            const uint64_t bytes = uint64_t(width) * height * 4;
            if (!width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
                height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || bytes > UINT32_MAX)
                throw std::invalid_argument("Invalid DX12 texture dimensions");
            return static_cast<uint32_t>(bytes);
        }
    }
    DX12Texture2D::DX12Texture2D(const std::string& path) : m_Device(DX12Context::Current().SharedDevice())
    {
        int width = 0, height = 0, channels = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load(path.c_str(), &width, &height, &channels, 4), stbi_image_free);
        if (!pixels || width <= 0 || height <= 0) throw std::runtime_error("Cannot load DX12 texture: " + path);
        m_Width = static_cast<uint32_t>(width); m_Height = static_cast<uint32_t>(height);
        Replace(pixels.get(), PixelBytes(m_Width, m_Height));
    }
    DX12Texture2D::DX12Texture2D(uint32_t width, uint32_t height) : DX12Texture2D(DX12Context::Current(), width, height) {}
    DX12Texture2D::DX12Texture2D(DX12Context& context, uint32_t width, uint32_t height, const void* pixels)
        : m_Device(context.SharedDevice()), m_Width(width), m_Height(height)
    {
        const auto bytes = PixelBytes(width, height);
        if (pixels) Replace(pixels, bytes);
        else { std::vector<uint8_t> zero(bytes, 0); Replace(zero.data(), bytes); }
    }
    DX12Texture2D::~DX12Texture2D() { if (m_Handle) m_Device->UnregisterTexture(m_Handle); }
    void DX12Texture2D::SetData(void* data, uint32_t size) { Replace(data, size); }
    void DX12Texture2D::Replace(const void* data, uint32_t size)
    {
        if (!data || size != PixelBytes(m_Width, m_Height)) throw std::invalid_argument("DX12 texture upload size mismatch");
        auto& context = DX12Context::CurrentFor(m_Device);
        auto image = context.CreateImage(m_Width, m_Height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
        context.Upload(image, data, size);
        if (m_Handle) m_Device->UpdateTexture(m_Handle, std::move(image));
        else m_Handle = m_Device->RegisterTexture(std::move(image));
    }
}
#endif
