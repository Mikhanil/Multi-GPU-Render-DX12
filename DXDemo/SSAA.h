#pragma once
#include "GMemory.h"
#include "GTexture.h"

class SSAA
{
	GTexture renderTarget;
	GTexture depthMap;
	
	GMemory srvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	GMemory rtvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
	GMemory dsvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

	UINT ResolutionMultiplier = 1;
	const DXGI_FORMAT rtvFormat = GetSRGBFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
	const DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	
	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

public:

	D3D12_VIEWPORT GetViewPort() const;

	D3D12_RECT GetRect() const;


	GTexture& GetRenderTarget();

	GTexture& GetDepthMap();

	GMemory* GetRTV();

	GMemory* GetSRV();

	GMemory* GetDSV();

	void OnResize(UINT newWidth, UINT newHeight);

	SSAA(UINT multiplier, UINT width, UINT height);
};

