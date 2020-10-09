#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "GRootSignature.h"
#include "DirectXBuffers.h"
#include "GShader.h"
#include "SimpleMath.h"

struct GenerateMipsCB
{
	DirectX::SimpleMath::Vector2 TexelSize; // 1.0 / OutMip1.Dimensions
};


class ComputePSO
{
public:
	ComputePSO();

	ComPtr<ID3D12PipelineState> GetPSO() const;

	void SetRootSignature(GRootSignature& sign);

	void SetShader(GShader* shader);

	void Initialize(std::shared_ptr<GDevice> device);
private:


	D3D12_COMPUTE_PIPELINE_STATE_DESC computePSOdesc{};

	ComPtr<ID3D12PipelineState> m_PipelineState;
};
