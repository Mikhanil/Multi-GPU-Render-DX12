#include "GraphicPSO.h"

#include "GDevice.h"

GraphicPSO::GraphicPSO(PsoType::Type type): type(type)
{
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
}

ComPtr<ID3D12PipelineState> GraphicPSO::GetPSO() const
{
	return pipelineStateObject;
}

void GraphicPSO::SetPsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC desc)
{
	psoDesc = desc;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC GraphicPSO::GetPsoDescription() const
{
	return psoDesc;
}

void GraphicPSO::SetRootSignature(ID3D12RootSignature* rootSign)
{
	psoDesc.pRootSignature = rootSign;
}

void GraphicPSO::SetInputLayout(D3D12_INPUT_LAYOUT_DESC layoutDesc)
{
	psoDesc.InputLayout = layoutDesc;
}

void GraphicPSO::SetRasterizationState(D3D12_RASTERIZER_DESC rastState)
{
	psoDesc.RasterizerState = rastState;
}

void GraphicPSO::SetRenderTargetBlendState(UINT index, D3D12_RENDER_TARGET_BLEND_DESC desc)
{
	psoDesc.BlendState.RenderTarget[index] = desc;
}

void GraphicPSO::SetBlendState(D3D12_BLEND_DESC blendDesc)
{
	psoDesc.BlendState = blendDesc;
}

void GraphicPSO::SetDepthStencilState(D3D12_DEPTH_STENCIL_DESC ddsDesc)
{
	psoDesc.DepthStencilState = ddsDesc;
}

void GraphicPSO::SetDSVFormat(DXGI_FORMAT format)
{
	psoDesc.DSVFormat = format;
}

void GraphicPSO::SetSampleMask(UINT sampleMask)
{
	psoDesc.SampleMask = sampleMask;
}

void GraphicPSO::SetPrimitiveType(D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveType)
{
	psoDesc.PrimitiveTopologyType = primitiveType;
}

void GraphicPSO::SetRenderTargetsCount(UINT count)
{
	psoDesc.NumRenderTargets = count;
}

void GraphicPSO::SetRTVFormat(UINT index, DXGI_FORMAT format)
{
	index = std::clamp(index, 0U, 7U);
	psoDesc.RTVFormats[index] = format;
}

void GraphicPSO::SetSampleCount(UINT count)
{
	psoDesc.SampleDesc.Count = count;
}

void GraphicPSO::SetSampleQuality(UINT quality)
{
	psoDesc.SampleDesc.Quality = quality;
}

void GraphicPSO::SetShader(GShader* shader)
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

PsoType::Type GraphicPSO::GetType() const
{
	return type;
}

void GraphicPSO::Initialize(std::shared_ptr<GDevice> device)
{
	if (isInitial) return;

	ThrowIfFailed(device->GetDXDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));

	isInitial = true;
}
