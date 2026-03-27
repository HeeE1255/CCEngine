#pragma once
#include "Renderer/Shader.h"
#include <d3d11.h>

namespace CCEngine
{
    class DX11Shader : public Shader
    {
    public:
        DX11Shader(const std::string& filepath);
        virtual ~DX11Shader();

        virtual void Bind() const override;
        virtual void Unbind() const override;

        virtual void BindLayout(const BufferLayout& layout) override;

    private:
        // DX11 전용 셰이더 객체들
        ID3D11VertexShader* m_VertexShader = nullptr;
        ID3D11PixelShader* m_PixelShader = nullptr;

        
        ID3DBlob* m_VSBlob = nullptr;               // 셰이더 원본 데이터 보관용
        ID3D11InputLayout* m_InputLayout = nullptr; // 정점 입력 레이아웃
    };
}