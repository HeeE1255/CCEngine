#include "Platform/DirectX11/DX11Shader.h"
#include "Platform/DirectX11/DX11Context.h"
#include <d3dcompiler.h> 
#include <iostream>

namespace CCEngine
{
    static DXGI_FORMAT ShaderDataTypeToDXGIFormat(ShaderDataType type)
    {
        switch (type)
        {
        case ShaderDataType::Float:  return DXGI_FORMAT_R32_FLOAT;
        case ShaderDataType::Float2: return DXGI_FORMAT_R32G32_FLOAT;
        case ShaderDataType::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case ShaderDataType::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case ShaderDataType::Int:    return DXGI_FORMAT_R32_SINT;
        case ShaderDataType::Int2:   return DXGI_FORMAT_R32G32_SINT;
        case ShaderDataType::Int3:   return DXGI_FORMAT_R32G32B32_SINT;
        case ShaderDataType::Int4:   return DXGI_FORMAT_R32G32B32A32_SINT;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    DX11Shader::DX11Shader(const std::string& filepath)
    {
        std::wstring pathWide(filepath.begin(), filepath.end());

        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;

        // ==========================================
        // 1. 정점 셰이더(VS) 컴파일
        // ==========================================
        HRESULT hr = D3DCompileFromFile(
            pathWide.c_str(), nullptr, nullptr,
            "VSMain", "vs_5_0", // 원래 쓰던 이름
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0,
            &vsBlob, &errorBlob
        );

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                std::cout << "[Shader Error] 정점 셰이더 컴파일 실패: "
                    << (char*)errorBlob->GetBufferPointer() << std::endl;
                errorBlob->Release();
            }
            return;
        }

        if (errorBlob)
        {
            errorBlob->Release();
        }

        // ==========================================
        // 2. 픽셀 셰이더(PS) 컴파일
        // ==========================================
        hr = D3DCompileFromFile(
            pathWide.c_str(), nullptr, nullptr,
            "PSMain", "ps_5_0", // 원래 쓰던 이름
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG, 0,
            &psBlob, &errorBlob
        );

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                std::cout << "[Shader Error] 픽셀 셰이더 컴파일 실패: "
                    << (char*)errorBlob->GetBufferPointer() << std::endl;
                errorBlob->Release();
            }
            return;
        }

        if (errorBlob)
        {
            errorBlob->Release();
        }

        auto device = DX11Context::Get()->GetDevice();

        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_VertexShader);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_PixelShader);

        m_VSBlob = vsBlob;
        psBlob->Release();

        std::cout << "[Shader] 셰이더 로드 및 컴파일 완료: " << filepath << std::endl;
    }

    DX11Shader::~DX11Shader()
    {
        if (m_InputLayout)
        {
            m_InputLayout->Release();
        }

        if (m_VSBlob)
        {
            m_VSBlob->Release();
        }

        if (m_PixelShader)
        {
            m_PixelShader->Release();
        }

        if (m_VertexShader)
        {
            m_VertexShader->Release();
        }
    }

    void DX11Shader::Bind() const
    {
        auto deviceContext = DX11Context::Get()->GetDeviceContext();

        deviceContext->IASetInputLayout(m_InputLayout);
        deviceContext->VSSetShader(m_VertexShader, nullptr, 0);
        deviceContext->PSSetShader(m_PixelShader, nullptr, 0);
    }

    void DX11Shader::Unbind() const
    {
        auto deviceContext = DX11Context::Get()->GetDeviceContext();

        deviceContext->IASetInputLayout(nullptr);
        deviceContext->VSSetShader(nullptr, nullptr, 0);
        deviceContext->PSSetShader(nullptr, nullptr, 0);
    }

    void DX11Shader::BindLayout(const BufferLayout& layout)
    {
        if (m_InputLayout == nullptr)
        {
            std::vector<D3D11_INPUT_ELEMENT_DESC> dx11Layout;

            for (const auto& element : layout)
            {
                D3D11_INPUT_ELEMENT_DESC desc = {};
                desc.SemanticName = element.Name.c_str();
                desc.SemanticIndex = 0;
                desc.Format = ShaderDataTypeToDXGIFormat(element.Type);
                desc.InputSlot = 0;
                desc.AlignedByteOffset = element.Offset;
                desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                desc.InstanceDataStepRate = 0;

                dx11Layout.push_back(desc);
            }

            DX11Context::Get()->GetDevice()->CreateInputLayout(
                dx11Layout.data(),
                (UINT)dx11Layout.size(),
                m_VSBlob->GetBufferPointer(),
                m_VSBlob->GetBufferSize(),
                &m_InputLayout
            );
        }

        DX11Context::Get()->GetDeviceContext()->IASetInputLayout(m_InputLayout);
    }
}