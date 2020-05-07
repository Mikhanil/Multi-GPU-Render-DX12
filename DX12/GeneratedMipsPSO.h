#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "RootSignature.h"
#include "ShapesApp.h"

struct alignas(16) GenerateMipsCB
{
    uint32_t SrcMipLevel;	// Texture level of source mip
    uint32_t NumMipLevels;	// Number of OutMips to write: [1-4]
    uint32_t SrcDimension;  // Width and height of the source texture are even or odd.
    uint32_t Padding;       // Pad to 16 byte alignment.
    DirectX::XMFLOAT2 TexelSize;	// 1.0 / OutMip1.Dimensions
};


class GeneratedMipsPSO
{
public:
	GeneratedMipsPSO(ID3D12Device* device);;

	const RootSignature& GetRootSignature() const;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipelineState() const;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GetUAVHeap() const;

private:
    RootSignature m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;

    Microsoft::WRL::ComPtr < ID3D12DescriptorHeap> UAVdescriptorHeap;
};

