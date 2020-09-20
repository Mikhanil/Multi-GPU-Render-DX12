#pragma once
#include "MemoryAllocator.h"
#include "AssetsLoader.h"
#include "d3dApp.h"
#include "FrameResource.h"

class MultiAdapter :
    public DXLib::D3DApp
{
public:
	MultiAdapter(HINSTANCE hInstance)
		: D3DApp(hInstance)		 
	{
	}
	
	bool Initialize() override;
	
protected:
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;

	void OnResize() override;
	
private:
	UINT backBufferIndex = 0;
	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	
	D3D12_VIEWPORT primeViewport;
	D3D12_RECT primeRect;
	D3D12_RECT secondRect;

	custom_vector<std::unique_ptr<FrameResource>> frameResources = MemoryAllocator::CreateVector<std::unique_ptr<FrameResource>>();

	FrameResource* currentFrameResource = nullptr;
	int currentFrameResourceIndex = 0;

	

public:

	
};

