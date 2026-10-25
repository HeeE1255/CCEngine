#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include <vector>
#include <string>
#include "Renderer/Buffer.h"
namespace CCEngine
{
    using Microsoft::WRL::ComPtr;
    class DX12Context;
    class DX12Device;
    void CheckDX12(HRESULT result, const char* operation);
    struct DX12BufferAllocation
    {
        std::weak_ptr<DX12Device> Owner;
        ComPtr<ID3D12Resource> Resource;
        uint32_t Size = 0;
    };
    struct DX12Image
    {
        std::weak_ptr<DX12Device> Owner;
        ComPtr<ID3D12Resource> Resource;
        ComPtr<ID3D12DescriptorHeap> SRV;
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
        DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
        uint32_t Width = 0, Height = 0;
    };
    struct DX12Target
    {
        std::weak_ptr<DX12Device> Owner;
        std::vector<std::shared_ptr<DX12Image>> Colors;
        std::shared_ptr<DX12Image> Depth;
        ComPtr<ID3D12DescriptorHeap> RTV, DSV;
        uint32_t Width = 0, Height = 0;
        bool Backbuffer = false;
        DX12Context* WindowOwner = nullptr;
    };
    struct DX12Program
    {
        std::weak_ptr<DX12Device> Owner;
        uint64_t ID = 0;
        std::vector<uint8_t> Vertex, Pixel;
        std::vector<VertexInput> Inputs;
        bool HasInputs = false;
    };
}
#endif
