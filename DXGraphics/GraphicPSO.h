#pragma once

#include "d3dUtil.h"
#include "GShader.h"

using namespace Microsoft::WRL;

class PsoType
{
public:
	enum Type
	{
		SkyBox,
		Opaque,
		Wireframe,
		OpaqueAlphaDrop,
		AlphaSprites,
		Mirror,
		Reflection,
		ShadowMapOpaque,
		ShadowMapOpaqueDrop,
		Transparent,
		DrawNormalsOpaque,
		DrawNormalsOpaqueDrop,
		Ssao,
		SsaoBlur,
		Debug,
		Quad,
		Count
	};
};

class GDevice;

class GraphicPSO
{
	ComPtr<ID3D12PipelineState> pipelineStateObject;
	PsoType::Type type;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

	bool isInitial = false;

public:

	GraphicPSO(PsoType::Type type = PsoType::Opaque);

	ComPtr<ID3D12PipelineState> GetPSO() const;

	void SetPsoDesc(D3D12_GRAPHICS_PIPELINE_STATE_DESC desc);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC GetPsoDescription() const;

	void SetRootSignature(ID3D12RootSignature* rootSign);

	void SetInputLayout(D3D12_INPUT_LAYOUT_DESC layoutDesc);

	void SetRasterizationState(D3D12_RASTERIZER_DESC rastState);

	void SetRenderTargetBlendState(UINT index, D3D12_RENDER_TARGET_BLEND_DESC desc);

	void SetBlendState(D3D12_BLEND_DESC blendDesc);

	void SetDepthStencilState(D3D12_DEPTH_STENCIL_DESC ddsDesc);

	void SetDSVFormat(DXGI_FORMAT format);

	void SetSampleMask(UINT sampleMask);

	void SetPrimitiveType(D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveType);

	void SetRenderTargetsCount(UINT count);

	void SetRTVFormat(UINT index, DXGI_FORMAT format);

	void SetSampleCount(UINT count);

	void SetSampleQuality(UINT quality);

	void SetShader(GShader* shader);

	PsoType::Type GetType() const;

	void Initialize(::std::shared_ptr<GDevice> device);
};
