#pragma once

#include "d3dUtil.h"
#include "GMemory.h"
#include "GTexture.h"
using namespace Microsoft::WRL;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class ShadowMap
{
public:
	ShadowMap(std::shared_ptr<GDevice> device, UINT width, UINT height);

	ShadowMap(const ShadowMap& rhs) = delete;
	ShadowMap& operator=(const ShadowMap& rhs) = delete;
	~ShadowMap() = default;

	UINT Width() const;
	UINT Height() const;
	GTexture& GetTexture();

	GMemory* GetSrvMemory()
	{
		return &srvMemory;
	}

	GMemory* GetDsvMemory()
	{
		return &dsvMemory;
	}
	
	D3D12_VIEWPORT Viewport() const;
	D3D12_RECT ScissorRect() const;

	void BuildDescriptors();

	void OnResize(UINT newWidth, UINT newHeight);

private:
	
	void BuildResource();

private:
	std::shared_ptr<GDevice> device;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;

	UINT width = 0;
	UINT height = 0;

	GMemory srvMemory;	
	GMemory dsvMemory;
	GTexture shadowMap;
};
