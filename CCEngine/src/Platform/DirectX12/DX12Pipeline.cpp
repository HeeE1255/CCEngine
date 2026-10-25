#include "DX12Context.h"
#if defined(CC_ENABLE_DX12_RHI)
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <climits>
namespace CCEngine
{
    ID3D12PipelineState* DX12Context::Pipeline(RendererAPI::BlendMode blend, RendererAPI::CullMode cull, bool depth)
    {
        std::ostringstream key;
        key << m_Program->ID << '/' << int(blend) << '/' << int(cull) << '/' << depth << '/' << m_Layout.GetStride();
        for (auto& color : m_Target->Colors) key << '/' << int(color->Format);
        std::vector<std::string> semantics;
        std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
        semantics.reserve(m_Layout.GetElements().size());
        for (const auto& element : m_Layout)
        {
            UINT columns = 1, components = 0; bool integer = false;
            switch (element.Type)
            {
            case ShaderDataType::Float: components = 1; break;
            case ShaderDataType::Float2: components = 2; break;
            case ShaderDataType::Float3: components = 3; break;
            case ShaderDataType::Float4: components = 4; break;
            case ShaderDataType::Int: components = 1; integer = true; break;
            case ShaderDataType::Int2: components = 2; integer = true; break;
            case ShaderDataType::Int3: components = 3; integer = true; break;
            case ShaderDataType::Int4: components = 4; integer = true; break;
            case ShaderDataType::Mat3: components = columns = 3; break;
            case ShaderDataType::Mat4: components = columns = 4; break;
            default: throw std::invalid_argument("Unsupported DX12 vertex type");
            }
            if (element.Name.empty() || (integer && element.Normalized) || element.Offset > m_Layout.GetStride() ||
                components * columns * 4 > m_Layout.GetStride() - element.Offset)
                throw std::invalid_argument("Invalid DX12 vertex attribute");
            size_t suffix = element.Name.size();
            while (suffix && std::isdigit(static_cast<unsigned char>(element.Name[suffix - 1]))) --suffix;
            if (!suffix) throw std::invalid_argument("Vertex semantic needs a name");
            semantics.push_back(element.Name.substr(0, suffix));
            const unsigned long semanticIndex = suffix == element.Name.size() ? 0 : std::stoul(element.Name.substr(suffix));
            if (semanticIndex > UINT32_MAX - columns) throw std::out_of_range("Vertex semantic index");
            const DXGI_FORMAT floats[] = { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT };
            const DXGI_FORMAT ints[] = { DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32A32_SINT };
            for (UINT column = 0; column < columns; ++column)
            {
                if (elements.size() >= 32) throw std::invalid_argument("Too many DX12 vertex attributes");
                elements.push_back({ semantics.back().c_str(), static_cast<UINT>(semanticIndex) + column,
                    integer ? ints[components - 1] : floats[components - 1], 0,
                    element.Offset + column * components * 4, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 });
            }
            key << '/' << element.Name.size() << ':' << element.Name << ':' << int(element.Type) << ':' << element.Offset;
        }
        const auto cached = m_Pipelines.find(key.str());
        if (cached != m_Pipelines.end()) return cached->second.Get();
        D3D12_GRAPHICS_PIPELINE_STATE_DESC info{};
        info.pRootSignature = m_Root.Get();
        info.VS = { m_Program->Vertex.data(), m_Program->Vertex.size() };
        info.PS = { m_Program->Pixel.data(), m_Program->Pixel.size() };
        info.InputLayout = { elements.data(), static_cast<UINT>(elements.size()) };
        info.SampleMask = UINT_MAX; info.SampleDesc.Count = 1; info.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        info.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        info.RasterizerState.CullMode = cull == RendererAPI::CullMode::Back ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
        info.RasterizerState.DepthClipEnable = TRUE;
        info.DepthStencilState.DepthEnable = depth;
        info.DepthStencilState.DepthWriteMask = depth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        info.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        info.DepthStencilState.StencilReadMask = info.DepthStencilState.StencilWriteMask = 0xff;
        const D3D12_DEPTH_STENCILOP_DESC stencil{ D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
        info.DepthStencilState.FrontFace = info.DepthStencilState.BackFace = stencil;
        info.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        info.NumRenderTargets = static_cast<UINT>(m_Target->Colors.size());
        info.BlendState.IndependentBlendEnable = TRUE;
        for (UINT i = 0; i < 8; ++i)
        {
            auto& attachment = info.BlendState.RenderTarget[i];
            attachment.BlendEnable = i == 0 && blend != RendererAPI::BlendMode::Opaque;
            attachment.SrcBlend = D3D12_BLEND_SRC_ALPHA; attachment.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            attachment.BlendOp = attachment.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            attachment.SrcBlendAlpha = D3D12_BLEND_ONE; attachment.DestBlendAlpha = D3D12_BLEND_ZERO;
            attachment.LogicOp = D3D12_LOGIC_OP_NOOP; attachment.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            if (i < info.NumRenderTargets) info.RTVFormats[i] = m_Target->Colors[i]->Format;
        }
        ComPtr<ID3D12PipelineState> pipeline;
        CheckDX12(m_Device->CreateGraphicsPipelineState(&info, IID_PPV_ARGS(&pipeline)), "Create DX12 graphics pipeline");
        return m_Pipelines.emplace(key.str(), std::move(pipeline)).first->second.Get();
    }
    void DX12Context::BindDescriptors()
    {
        // draw마다 32개 슬롯의 스냅샷을 할당한다. fence 이전에는 이미 기록된 descriptor를 덮어쓰지 않는다.
        if (m_PageUsed + 32 > 1024)
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc{}; desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 1024; desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ComPtr<ID3D12DescriptorHeap> page;
            CheckDX12(m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&page)), "Create DX12 descriptor page");
            m_Pages.push_back(std::move(page)); m_PageUsed = 0;
        }
        auto* heap = m_Pages.back().Get();
        const UINT step = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += SIZE_T(m_PageUsed) * step;
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += UINT64(m_PageUsed) * step;
        for (UINT i = 0; i < 32; ++i)
        {
            auto image = m_SharedDevice->ResolveTexture(m_Textures[i]);
            if (!image) image = m_EmptyTexture;
            for (const auto& color : m_Target->Colors)
                if (image == color) throw std::logic_error("Cannot sample an active DX12 render target");
            Transition(image, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            m_Device->CopyDescriptorsSimple(1, cpu, image->SRV->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            cpu.ptr += step;
        }
        m_PageUsed += 32;
        m_List->SetDescriptorHeaps(1, &heap);
        m_List->SetGraphicsRootDescriptorTable(8, gpu);
        for (UINT i = 0; i < 8; ++i)
        {
            auto buffer = m_Uniforms[i] ? m_Uniforms[i] : m_EmptyUniform;
            m_Pending.push_back(buffer);
            m_List->SetGraphicsRootConstantBufferView(i, buffer->Resource->GetGPUVirtualAddress());
        }
    }
    void DX12Context::Draw(uint32_t count, RendererAPI::BlendMode blend, RendererAPI::CullMode cull, bool depth)
    {
        if (!count) return;
        if (!m_Target || !m_Program || !m_Vertex || !m_Index || count > m_IndexCount)
            throw std::logic_error("DX12 draw requires valid target, shader and buffers");
        if (!m_Layout.GetStride() || m_Layout.GetStride() > 2048 || m_Layout.GetStride() > m_Vertex->Size)
            throw std::invalid_argument("Invalid DX12 vertex stride");
        // 바인딩 순서와 캐시 적중 여부에 관계없이 실제 draw의 입력 계약을 먼저 검사한다.
        if (m_Program->HasInputs) ResolveVertexInputs(m_Layout, m_Program->Inputs);
        if (!m_Viewport.Width || !m_Viewport.Height ||
            (m_ScissorEnabled && (m_Scissor.left == m_Scissor.right || m_Scissor.top == m_Scissor.bottom))) return;
        auto* pipeline = Pipeline(blend, cull, depth);
        ApplyTarget();
        m_List->SetGraphicsRootSignature(m_Root.Get());
        m_List->SetPipelineState(pipeline); BindDescriptors();
        m_List->RSSetViewports(1, &m_Viewport);
        const D3D12_RECT scissor = m_ScissorEnabled ? m_Scissor : D3D12_RECT{ 0, 0, LONG(m_Target->Width), LONG(m_Target->Height) };
        m_List->RSSetScissorRects(1, &scissor);
        const D3D12_VERTEX_BUFFER_VIEW vertex{ m_Vertex->Resource->GetGPUVirtualAddress(), m_Vertex->Size, m_Layout.GetStride() };
        const D3D12_INDEX_BUFFER_VIEW index{ m_Index->Resource->GetGPUVirtualAddress(), m_Index->Size, DXGI_FORMAT_R32_UINT };
        m_List->IASetVertexBuffers(0, 1, &vertex); m_List->IASetIndexBuffer(&index);
        m_List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_Pending.push_back(m_Vertex); m_Pending.push_back(m_Index); m_Pending.push_back(m_Program);
        m_List->DrawIndexedInstanced(count, 1, 0, 0, 0);
    }
}
#endif
