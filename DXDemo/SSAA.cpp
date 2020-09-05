#include "SSAA.h"
#include "GCommandList.h"


D3D12_VIEWPORT SSAA::GetViewPort() const
{
	return viewport;
}

D3D12_RECT SSAA::GetRect() const
{
	return scissorRect;
}

GTexture& SSAA::GetRenderTarget()
{
	return renderTarget;
}

GTexture& SSAA::GetDepthMap()
{
	return depthMap;
}

GMemory* SSAA::GetRTV()
{
	return &rtvMemory;
}

GMemory* SSAA::GetSRV()
{
	return &srvMemory;
}

GMemory* SSAA::GetDSV()
{
	return &dsvMemory;
}

void SSAA::OnResize(UINT newWidth, UINT newHeight)
{
	if (viewport.Width == newWidth * ResolutionMultiplier && viewport.Height == newHeight * ResolutionMultiplier)
	{
		return;	
	}

	int width = newWidth * ResolutionMultiplier;
	int height = newHeight * ResolutionMultiplier;
	
	viewport.Height = static_cast<float>(height);
	viewport.Width = static_cast<float>(width);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	
	scissorRect = { 0,0, width , height };
	

	if (!renderTarget.IsValid())
	{
		D3D12_RESOURCE_DESC renderTargetDesc;
		renderTargetDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		renderTargetDesc.Alignment = 0;
		renderTargetDesc.Width = width;
		renderTargetDesc.Height = height;
		renderTargetDesc.DepthOrArraySize = 1;
		renderTargetDesc.MipLevels = 1;
		renderTargetDesc.Format = rtvFormat;
		renderTargetDesc.SampleDesc.Count = 1;
		renderTargetDesc.SampleDesc.Quality = 0;
		renderTargetDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		renderTargetDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		renderTarget = GTexture(renderTargetDesc, L"SSAA RTV", TextureUsage::RenderTarget);
	}
	else
	{
		GTexture::Resize(renderTarget, width, height, 1);
	}

	if (!depthMap.IsValid())
	{
		D3D12_RESOURCE_DESC depthStencilDesc;
		depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilDesc.Alignment = 0;
		depthStencilDesc.Width = width;
		depthStencilDesc.Height = height;
		depthStencilDesc.DepthOrArraySize = 1;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.Format = depthStencilFormat;
		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
		depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE optClear;
		optClear.Format = depthStencilFormat;
		optClear.DepthStencil.Depth = 1.0f;
		optClear.DepthStencil.Stencil = 0;

		depthMap = GTexture(depthStencilDesc, L"SSAA DSV", TextureUsage::Depth, &optClear);
	}
	else
	{
		GTexture::Resize(depthMap, width, height, 1);
	}
	

	
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = rtvFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	renderTarget.CreateRenderTargetView(&rtvDesc, &rtvMemory);
	

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = rtvFormat;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	renderTarget.CreateShaderResourceView(&srvDesc, &srvMemory);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = depthStencilFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	depthMap.CreateDepthStencilView(&dsvDesc, &dsvMemory);
}

SSAA::SSAA(UINT multiplier, UINT width, UINT height): ResolutionMultiplier(multiplier)
{
	OnResize(width, height);
}
