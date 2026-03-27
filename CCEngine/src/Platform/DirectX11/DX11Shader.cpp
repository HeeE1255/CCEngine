#include "Platform/DirectX11/DX11Shader.h"
#include "Platform/DirectX11/DX11Context.h"
#include <d3dcompiler.h> // 셰이더 컴파일러 헤더
#include <iostream>


namespace CCEngine
{
    //
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
        // 파일 경로를 wstring으로 변환 (DirectX 함수 요구사항)
        std::wstring pathWide(filepath.begin(), filepath.end());

        ID3DBlob* vsBlob = nullptr; // 컴파일된 정점 셰이더 데이터를 담을 바구니
        ID3DBlob* psBlob = nullptr; // 컴파일된 픽셀 셰이더 데이터를 담을 바구니
        ID3DBlob* errorBlob = nullptr; // 에러 메시지를 담을 바구니

        // ==========================================
        // 1. 정점 셰이더(VS) 컴파일
        // ==========================================
        HRESULT hr = D3DCompileFromFile(
            pathWide.c_str(), nullptr, nullptr,
            "VSMain", "vs_5_0", // "VSMain" 함수를 버전 5.0으로 컴파일
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
            "PSMain", "ps_5_0", // "PSMain" 함수를 버전 5.0으로 컴파일
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

        // ==========================================
        // 3. 디바이스를 가져와서 실제 셰이더 객체 생성
        // ==========================================
        auto device = DX11Context::Get()->GetDevice();

        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_VertexShader);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_PixelShader);

        // 정점 셰이더의 원본 데이터를 보관 (입력 레이아웃 생성 시 필요)
        m_VSBlob = vsBlob;

        //Blob 메모리 해제
        psBlob->Release();

        std::cout << "[Shader] 셰이더 로드 및 컴파일 완료: " << filepath << std::endl;
    }

    DX11Shader::~DX11Shader()
    {
        // 역순으로 안전하게 해제
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

        // 파이프라인에 입력 레이아웃, 정점 셰이더, 픽셀 셰이더를 적용
        deviceContext->IASetInputLayout(m_InputLayout);
        deviceContext->VSSetShader(m_VertexShader, nullptr, 0);
        deviceContext->PSSetShader(m_PixelShader, nullptr, 0);
    }

    void DX11Shader::Unbind() const
    {
        auto deviceContext = DX11Context::Get()->GetDeviceContext();

        // 해제할 때는 nullptr로 설정하여 파이프라인에서 제거
        deviceContext->IASetInputLayout(nullptr);
        deviceContext->VSSetShader(nullptr, nullptr, 0);
        deviceContext->PSSetShader(nullptr, nullptr, 0);
    }

    void DX11Shader::BindLayout(const BufferLayout& layout)
    {
        // 입력 레이아웃이 아직 생성되지 않았다면, BufferLayout을 기반으로 생성
        if (m_InputLayout == nullptr)
        {
            std::vector<D3D11_INPUT_ELEMENT_DESC> dx11Layout;

            // BufferLayout의 각 요소를 D3D11_INPUT_ELEMENT_DESC로 변환
            for (const auto& element : layout)
            {
                D3D11_INPUT_ELEMENT_DESC desc = {};
                desc.SemanticName = element.Name.c_str(); // 셰이더에서 사용할 시맨틱 이름 (예: POSITION, NORMAL, TEXCOORD)
                desc.SemanticIndex = 0; // 동일한 이름이 여러 개일 때 구분하는 인덱스 (예: TEXCOORD0, TEXCOORD1)
                desc.Format = ShaderDataTypeToDXGIFormat(element.Type); // 요소의 데이터 형식 변환
                desc.InputSlot = 0; // 정점 버퍼 슬롯 (여러 버퍼를 사용할 때 구분)
                desc.AlignedByteOffset = element.Offset; // 요소의 버퍼 내 위치
                desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA; // 정점 데이터로 사용할 때
                desc.InstanceDataStepRate = 0; // 인스턴스 데이터로 사용할 때는 1 이상으로 설정

                dx11Layout.push_back(desc);
            }

            // DirectX 11 디바이스를 사용하여 입력 레이아웃 생성
            DX11Context::Get()->GetDevice()->CreateInputLayout(
                dx11Layout.data(),
                (UINT)dx11Layout.size(),
                m_VSBlob->GetBufferPointer(),
                m_VSBlob->GetBufferSize(),
                &m_InputLayout
            );
        }

        // 레이아웃이 준비되었으면 바로 파이프라인에 적용
        DX11Context::Get()->GetDeviceContext()->IASetInputLayout(m_InputLayout);
    }


}