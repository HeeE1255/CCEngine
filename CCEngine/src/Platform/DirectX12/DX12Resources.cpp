#include "DX12Context.h"
#if defined(CC_ENABLE_DX12_RHI)
#include <cstring>
#include <stdexcept>
namespace CCEngine
{
    namespace
    {
        ComPtr<ID3D12Resource> TransferBuffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE type)
        {
            D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
            D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
            desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> resource;
            CheckDX12(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                type == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&resource)), "Create DX12 transfer buffer");
            return resource;
        }
    }
    std::shared_ptr<DX12BufferAllocation> DX12Context::CreateBuffer(uint32_t bytes, const void* data, bool constant)
    {
        m_SharedDevice->RequireUsable();
        if (!m_Device || m_Failed) throw std::logic_error("DX12 device unavailable");
        if (!bytes || (constant && bytes > 65536)) throw std::invalid_argument("Invalid DX12 buffer size");
        const uint64_t allocated = constant ? (uint64_t(bytes) + 255) & ~uint64_t(255) : bytes;
        auto buffer = std::make_shared<DX12BufferAllocation>(); buffer->Owner = m_SharedDevice; buffer->Size = bytes;
        buffer->Resource = TransferBuffer(m_Device.Get(), allocated, D3D12_HEAP_TYPE_UPLOAD);
        void* mapped = nullptr; const D3D12_RANGE none{ 0, 0 };
        CheckDX12(buffer->Resource->Map(0, &none, &mapped), "Map DX12 buffer");
        std::memset(mapped, 0, static_cast<size_t>(allocated));
        if (data) std::memcpy(mapped, data, bytes);
        buffer->Resource->Unmap(0, nullptr);
        return buffer;
    }
    std::shared_ptr<DX12Image> DX12Context::CreateImage(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
    {
        m_SharedDevice->RequireUsable();
        if (!m_Device || m_Failed) throw std::logic_error("DX12 device unavailable");
        if (!width || !height || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::invalid_argument("Invalid DX12 image size");
        if (format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_R32_SINT && format != DXGI_FORMAT_D32_FLOAT)
            throw std::invalid_argument("Unsupported DX12 image format");
        auto image = std::make_shared<DX12Image>(); image->Owner = m_SharedDevice;
        image->Width = width; image->Height = height; image->Format = format;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width; desc.Height = height; desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.Format = format; desc.SampleDesc.Count = 1; desc.Flags = flags;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        CheckDX12(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, image->State,
            nullptr, IID_PPV_ARGS(&image->Resource)), "Create DX12 image");
        if (format != DXGI_FORMAT_D32_FLOAT)
        {
            D3D12_DESCRIPTOR_HEAP_DESC srv{}; srv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; srv.NumDescriptors = 1;
            CheckDX12(m_Device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&image->SRV)), "Create DX12 SRV storage");
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
            m_Device->CreateShaderResourceView(image->Resource.Get(), &view, image->SRV->GetCPUDescriptorHandleForHeapStart());
        }
        return image;
    }
    std::shared_ptr<DX12Target> DX12Context::MakeTarget(std::vector<std::shared_ptr<DX12Image>> colors, bool backbuffer)
    {
        if (colors.empty() || colors.size() > 8) throw std::invalid_argument("Invalid DX12 target colors");
        auto target = std::make_shared<DX12Target>(); target->Owner = m_SharedDevice; target->Backbuffer = backbuffer;
        target->WindowOwner = backbuffer ? this : nullptr;
        target->Width = colors[0]->Width; target->Height = colors[0]->Height; target->Colors = std::move(colors);
        for (auto& image : target->Colors)
            if (image->Owner.lock() != m_SharedDevice || image->Width != target->Width || image->Height != target->Height)
                throw std::invalid_argument("DX12 target attachment mismatch");
        target->Depth = CreateImage(target->Width, target->Height, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = static_cast<UINT>(target->Colors.size());
        CheckDX12(m_Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&target->RTV)), "Create DX12 RTV heap");
        auto handle = target->RTV->GetCPUDescriptorHandleForHeapStart();
        const UINT step = m_Device->GetDescriptorHandleIncrementSize(heap.Type);
        for (auto& image : target->Colors)
        {
            m_Device->CreateRenderTargetView(image->Resource.Get(), nullptr, handle); handle.ptr += step;
        }
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; heap.NumDescriptors = 1;
        CheckDX12(m_Device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&target->DSV)), "Create DX12 DSV heap");
        m_Device->CreateDepthStencilView(target->Depth->Resource.Get(), nullptr, target->DSV->GetCPUDescriptorHandleForHeapStart());
        return target;
    }
    std::shared_ptr<DX12Target> DX12Context::CreateTarget(uint32_t width, uint32_t height)
    {
        return MakeTarget({ CreateImage(width, height, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
            CreateImage(width, height, DXGI_FORMAT_R32_SINT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) }, false);
    }
    void DX12Context::Upload(const std::shared_ptr<DX12Image>& image, const void* pixels, size_t bytes)
    {
        if (!image || image->Owner.lock() != m_SharedDevice || !pixels || image->Format == DXGI_FORMAT_D32_FLOAT ||
            uint64_t(image->Width) * image->Height * 4 != bytes) throw std::invalid_argument("Invalid DX12 image upload");
        auto desc = image->Resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 size = 0;
        m_Device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &size);
        auto staging = std::make_shared<ComPtr<ID3D12Resource>>(TransferBuffer(m_Device.Get(), size, D3D12_HEAP_TYPE_UPLOAD));
        void* mapped = nullptr; const D3D12_RANGE none{ 0, 0 };
        CheckDX12((*staging)->Map(0, &none, &mapped), "Map DX12 image upload");
        for (uint32_t y = 0; y < image->Height; ++y)
            std::memcpy(static_cast<uint8_t*>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch,
                static_cast<const uint8_t*>(pixels) + size_t(y) * image->Width * 4, size_t(image->Width) * 4);
        (*staging)->Unmap(0, nullptr);
        const auto previous = image->State;
        Transition(image, D3D12_RESOURCE_STATE_COPY_DEST); m_Pending.push_back(staging);
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = staging->Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = image->Resource.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        m_List->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        Transition(image, previous == D3D12_RESOURCE_STATE_COMMON ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : previous);
    }
    void DX12Context::Read(const std::shared_ptr<DX12Image>& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void* pixels)
    {
        if (!image || image->Owner.lock() != m_SharedDevice || !pixels || !width || !height || image->Format == DXGI_FORMAT_D32_FLOAT ||
            x > image->Width || y > image->Height || width > image->Width - x || height > image->Height - y)
            throw std::invalid_argument("Invalid DX12 readback");
        auto desc = image->Resource->GetDesc(); desc.Width = width; desc.Height = height;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes = 0;
        m_Device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        auto staging = std::make_shared<ComPtr<ID3D12Resource>>(TransferBuffer(m_Device.Get(), bytes, D3D12_HEAP_TYPE_READBACK));
        const auto previous = image->State; Transition(image, D3D12_RESOURCE_STATE_COPY_SOURCE);
        m_Pending.push_back(staging);
        D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource = image->Resource.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource = staging->Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        const D3D12_BOX box{ x, y, 0, x + width, y + height, 1 };
        m_List->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
        Transition(image, previous); Flush();
        void* mapped = nullptr; const D3D12_RANGE range{ 0, static_cast<SIZE_T>(bytes) };
        CheckDX12((*staging)->Map(0, &range, &mapped), "Map DX12 readback");
        // GPU 행 간격은 256바이트 정렬이다. CPU 결과는 빈 공간 없는 좌상단 기준 RGBA/ID 배열로 돌려준다.
        for (uint32_t row = 0; row < height; ++row)
            std::memcpy(static_cast<uint8_t*>(pixels) + size_t(row) * width * 4,
                static_cast<const uint8_t*>(mapped) + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch, size_t(width) * 4);
        const D3D12_RANGE none{ 0, 0 }; (*staging)->Unmap(0, &none);
    }
    void DX12Context::ClearID(const std::shared_ptr<DX12Target>& target, int32_t value)
    {
        if (!target || target->Owner.lock() != m_SharedDevice || target->Colors.size() < 2) throw std::invalid_argument("No DX12 ID attachment");
        // float clear 경로에서 큰 정수 ID가 반올림되는 것을 피한다. 모든 int32 값을 정확히 기록한다.
        std::vector<int32_t> pixels(size_t(target->Width) * target->Height, value);
        Upload(target->Colors[1], pixels.data(), pixels.size() * sizeof(int32_t));
    }
    void DX12Context::BindVertex(std::shared_ptr<DX12BufferAllocation> buffer, const BufferLayout& layout)
    {
        if (buffer && buffer->Owner.lock() != m_SharedDevice) throw std::invalid_argument("Foreign DX12 vertex buffer");
        m_Vertex = std::move(buffer); m_Layout = layout;
    }
    void DX12Context::BindIndex(std::shared_ptr<DX12BufferAllocation> buffer, uint32_t count)
    {
        if (buffer && (buffer->Owner.lock() != m_SharedDevice || uint64_t(count) * 4 > buffer->Size)) throw std::invalid_argument("Invalid DX12 index buffer");
        m_Index = std::move(buffer); m_IndexCount = count;
    }
    void DX12Context::BindUniform(uint32_t slot, std::shared_ptr<DX12BufferAllocation> buffer)
    {
        if (slot >= 8 || (buffer && (buffer->Owner.lock() != m_SharedDevice || buffer->Size > 65536 || buffer->Resource->GetDesc().Width % 256)))
            throw std::invalid_argument("Invalid DX12 constant buffer binding");
        m_Uniforms[slot] = std::move(buffer);
    }
    void DX12Context::BindProgram(std::shared_ptr<DX12Program> program)
    {
        if (program && program->Owner.lock() != m_SharedDevice) throw std::invalid_argument("Foreign DX12 program");
        m_Program = std::move(program);
    }
    void DX12Context::ReplaceBuffer(const std::shared_ptr<DX12BufferAllocation>& oldBuffer, const std::shared_ptr<DX12BufferAllocation>& next)
    {
        if (m_Vertex == oldBuffer) m_Vertex = next;
        if (m_Index == oldBuffer) m_Index = next;
        for (auto& buffer : m_Uniforms) if (buffer == oldBuffer) buffer = next;
    }
    RendererHandle DX12Context::RegisterTexture(std::shared_ptr<DX12Image> image)
    {
        return m_SharedDevice->RegisterTexture(std::move(image));
    }
    void DX12Context::UpdateTexture(RendererHandle handle, std::shared_ptr<DX12Image> image)
    {
        m_SharedDevice->UpdateTexture(handle, std::move(image));
    }
    void DX12Context::UnregisterTexture(RendererHandle handle)
    {
        m_SharedDevice->UnregisterTexture(handle);
    }
    void DX12Context::BindTexture(uint32_t slot, RendererHandle handle)
    {
        if (slot >= 32 || (handle && !m_SharedDevice->ResolveTexture(handle))) throw std::invalid_argument("Invalid DX12 texture slot/handle");
        m_Textures[slot] = handle;
    }
}
#endif
