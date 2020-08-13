#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "RootSignature.h"
#include "DirectXBuffers.h"
#include "Shader.h"

struct GenerateMipsCB
{
	DirectX::SimpleMath::Vector2 TexelSize; // 1.0 / OutMip1.Dimensions
};


class ComputePSO
{
public:
	ComputePSO();

	ComPtr<ID3D12PipelineState> GetPSO() const;

	void SetRootSignature(RootSignature& sign);

	void SetShader(Shader* shader);

	void Initialize();
private:


	D3D12_COMPUTE_PIPELINE_STATE_DESC computePSOdesc{};

	ComPtr<ID3D12PipelineState> m_PipelineState;

};
