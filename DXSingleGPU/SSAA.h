#pragma once
#include "GMemory.h"
#include "GTexture.h"

class SSAA
{
	GTexture renderTarget;
	GTexture depthMap;
	
	GMemory srvMemory;
	GMemory rtvMemory;
	GMemory dsvMemory;

	UINT ResolutionMultiplier = 1;
	const DXGI_FORMAT rtvFormat = GetSRGBFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
	const DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	std::shared_ptr<GDevice> device;
	
public:
		
	D3D12_VIEWPORT GetViewPort() const;

	D3D12_RECT GetRect() const;


	GTexture& GetRenderTarget();

	GTexture& GetDepthMap();

	GMemory* GetRTV();

	GMemory* GetSRV();

	GMemory* GetDSV();

	void OnResize(UINT newWidth, UINT newHeight);

	SSAA(const std::shared_ptr<GDevice> device, UINT multiplier, UINT width, UINT height);
};

