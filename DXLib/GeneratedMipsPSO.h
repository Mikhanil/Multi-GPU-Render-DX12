#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "RootSignature.h"
#include "DirectXBuffers.h"

struct GenerateMipsCB
{
	DirectX::SimpleMath::Vector2 TexelSize; // 1.0 / OutMip1.Dimensions
};


class GeneratedMipsPSO
{
public:
	GeneratedMipsPSO(ID3D12Device* device);;

	const ComPtr<ID3D12RootSignature> GetRootSignature() const;

	ComPtr<ID3D12PipelineState> GetPipelineState() const;

	ComPtr<ID3D12DescriptorHeap> GetUAVHeap() const;

	ConstantBuffer<GenerateMipsCB>* GetBuffer() const
	{
		return mipsBuffer.get();
	}

private:

	std::unique_ptr<ConstantBuffer<GenerateMipsCB>> mipsBuffer;

	RootSignature m_RootSignature;
	ComPtr<ID3D12PipelineState> m_PipelineState;

	ComPtr<ID3D12DescriptorHeap> UAVdescriptorHeap;
};
