#include "ComputePSO.h"

#include "d3dApp.h"
#include "GShader.h"

ComputePSO::ComputePSO()
{
}


void ComputePSO::Initialize()
{
	auto& device = DXLib::D3DApp::GetApp().GetDevice();
	ThrowIfFailed(device.CreateComputePipelineState(&computePSOdesc, IID_PPV_ARGS(&m_PipelineState)));

}

ComPtr<ID3D12PipelineState> ComputePSO::GetPSO() const
{
	return m_PipelineState;
}

void ComputePSO::SetRootSignature(GRootSignature& sign)
{
	computePSOdesc.pRootSignature = sign.GetRootSignature().Get();
}

void ComputePSO::SetShader(GShader* shader)
{
	if (shader->GetType() == ComputeShader)
	{
		computePSOdesc.CS = shader->GetShaderResource();
		return;
	}

	assert("Bad Shader");
}
