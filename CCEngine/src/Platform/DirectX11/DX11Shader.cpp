#include "Platform/DirectX11/DX11Shader.h"
#include "Platform/DirectX11/DX11Context.h"
#include <d3dcompiler.h> 
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <wrl/client.h>
#include <d3d11shader.h>

namespace CCEngine
{
    bool DX11Shader::ReadTextureBindings(const void* bytes, size_t size)
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytes, size, IID_PPV_ARGS(&reflection))))
            return false;
        D3D11_SHADER_DESC desc{};
        if (FAILED(reflection->GetDesc(&desc))) return false;
        TextureBindings bindings;
        for (UINT i = 0; i < desc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC resource{};
            if (FAILED(reflection->GetResourceBindingDesc(i, &resource))) return false;
            if (resource.Type != D3D_SIT_TEXTURE) continue;
            for (UINT element = 0; element < resource.BindCount; ++element)
            {
                std::string name = resource.Name;
                if (resource.BindCount > 1) name += "[" + std::to_string(element) + "]";
                bindings.emplace(name, resource.BindPoint + element);
            }
        }
        try { SetTextureBindings(std::move(bindings)); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return false; }
        return true;
    }
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

    namespace
    {
        bool ReadBytecodeFile(const std::filesystem::path& path, std::vector<char>& outBytes)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input.is_open())
                return false;

            const std::streamsize size = input.tellg();
            if (size <= 0)
                return false;

            input.seekg(0, std::ios::beg);
            outBytes.resize(static_cast<size_t>(size));
            return input.read(outBytes.data(), size).good();
        }

        ID3DBlob* MakeBlobCopy(const std::vector<char>& bytes)
        {
            ID3DBlob* blob = nullptr;
            if (FAILED(D3DCreateBlob(bytes.size(), &blob)) || !blob)
                return nullptr;

            memcpy(blob->GetBufferPointer(), bytes.data(), bytes.size());
            return blob;
        }
    }

    DX11Shader::DX11Shader(const std::string& filepath)
    {
        SetMaterialPropertySource(ShaderPropertyParser::LoadDeclarationSource(filepath));
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

        if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_VertexShader)) ||
            FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_PixelShader)))
        {
            vsBlob->Release();
            psBlob->Release();
            return;
        }

        m_VSBlob = vsBlob;
        const bool bindingsValid = ReadTextureBindings(psBlob->GetBufferPointer(), psBlob->GetBufferSize());
        psBlob->Release();
        m_IsValid = bindingsValid;

        std::cout << "[Shader] 셰이더 로드 및 컴파일 완료: " << filepath << std::endl;
    }

    DX11Shader::DX11Shader(const std::filesystem::path& vertexBytecode, const std::filesystem::path& pixelBytecode)
    {
        std::vector<char> vertexBytes;
        std::vector<char> pixelBytes;
        if (!ReadBytecodeFile(vertexBytecode, vertexBytes) || !ReadBytecodeFile(pixelBytecode, pixelBytes))
        {
            std::cout << "[Shader Error] 셰이더 바이트코드를 읽을 수 없음: "
                << vertexBytecode.string() << " / " << pixelBytecode.string() << std::endl;
            return;
        }

        auto device = DX11Context::Get()->GetDevice();
        if (FAILED(device->CreateVertexShader(vertexBytes.data(), vertexBytes.size(), nullptr, &m_VertexShader)) ||
            FAILED(device->CreatePixelShader(pixelBytes.data(), pixelBytes.size(), nullptr, &m_PixelShader)))
        {
            std::cout << "[Shader Error] 셰이더 바이트코드로 GPU 셰이더 생성 실패: "
                << vertexBytecode.string() << " / " << pixelBytecode.string() << std::endl;
            return;
        }

        // InputLayout은 정점 셰이더 바이트코드가 있어야 만든다.
        // 그래서 파일에서 읽은 VS bytecode를 Blob으로 복사해 DX11Shader가 소유하게 둔다.
        m_VSBlob = MakeBlobCopy(vertexBytes);
        if (!m_VSBlob)
            return;

        m_IsValid = ReadTextureBindings(pixelBytes.data(), pixelBytes.size());
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
        if (!m_IsValid)
            return;

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
        layout.Validate();
        std::string key = std::to_string(layout.GetStride());
        for (const auto& element : layout)
            key += "/" + element.Name + ":" + std::to_string(static_cast<int>(element.Type))
                + ":" + std::to_string(element.Offset);
        // 같은 셰이더라도 다른 정점 버퍼를 그리면 이전 InputLayout을 재사용할 수 없다.
        if (m_InputLayout && key != m_InputLayoutKey)
        {
            m_InputLayout->Release();
            m_InputLayout = nullptr;
        }
        if (m_InputLayout == nullptr)
        {
            if (!m_IsValid || !m_VSBlob)
                return;

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

            const HRESULT result = DX11Context::Get()->GetDevice()->CreateInputLayout(
                dx11Layout.data(),
                (UINT)dx11Layout.size(),
                m_VSBlob->GetBufferPointer(),
                m_VSBlob->GetBufferSize(),
                &m_InputLayout
            );
            if (FAILED(result))
                throw std::runtime_error("DX11 vertex layout does not match the shader signature");
            m_InputLayoutKey = std::move(key);
        }

        DX11Context::Get()->GetDeviceContext()->IASetInputLayout(m_InputLayout);
    }
}
