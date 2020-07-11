#include "PSO.h"

PSO::PSO(PsoType::Type type): type(type)
{
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC PSO::GetPsoDescription() const
{
	return psoDesc;
}

void PSO::SetRootSignature(ID3D12RootSignature* rootSign)
{
	psoDesc.pRootSignature = rootSign;
}

void PSO::SetInputLayout(D3D12_INPUT_LAYOUT_DESC layoutDesc)
{
	psoDesc.InputLayout = layoutDesc;
}

void PSO::SetRasterizationState(D3D12_RASTERIZER_DESC rastState)
{
	psoDesc.RasterizerState = rastState;
}

void PSO::SetBlendState(D3D12_BLEND_DESC blendDesc)
{
	psoDesc.BlendState = blendDesc;
}

void PSO::SetDepthStencilState(D3D12_DEPTH_STENCIL_DESC ddsDesc)
{
	psoDesc.DepthStencilState = ddsDesc;
}

void PSO::SetDSVFormat(DXGI_FORMAT format)
{
	psoDesc.DSVFormat = format;
}

void PSO::SetSampleMask(UINT sampleMask)
{
	psoDesc.SampleMask = sampleMask;
}

void PSO::SetPrimitiveType(D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveType)
{
	psoDesc.PrimitiveTopologyType = primitiveType;
}

void PSO::SetRenderTargetsCount(UINT count)
{
	psoDesc.NumRenderTargets = count;
}

void PSO::SetRTVFormat(UINT index, DXGI_FORMAT format)
{
	index = std::clamp(index, 0U, 7U);
	psoDesc.RTVFormats[index] = format;
}

void PSO::SetSampleCount(UINT count)
{
	psoDesc.SampleDesc.Count = count;
}

void PSO::SetSampleQuality(UINT quality)
{
	psoDesc.SampleDesc.Quality = quality;
}

void PSO::SetShader(Shader* shader)
{
	switch (shader->GetType())
	{
	case VertexShader:
		{
			psoDesc.VS = shader->GetShaderResource();
			break;
		}
	case PixelShader:
		{
			psoDesc.PS = shader->GetShaderResource();
			break;
		}
	case DomainShader:
		{
			psoDesc.DS = shader->GetShaderResource();
			break;
		}
	case GeometryShader:
		{
			psoDesc.GS = shader->GetShaderResource();
			break;
		}
	case HullShader:
		{
			psoDesc.HS = shader->GetShaderResource();
			break;
		}
	default:
		{
			assert("Bad Shader!!");
		}
	}
}

PsoType::Type PSO::GetType() const
{
	return type;
}

void PSO::Initialize(ID3D12Device* device)
{
	if (isInitial) return;

	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&debugPso)));

	isInitial = true;
}
