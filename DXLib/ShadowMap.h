#pragma once

#include "d3dUtil.h"
#include "GMemory.h"
#include "Texture.h"
using namespace Microsoft::WRL;
class ShadowMap
{
public:
	ShadowMap(UINT width, UINT height);

	ShadowMap(const ShadowMap& rhs) = delete;
	ShadowMap& operator=(const ShadowMap& rhs) = delete;
	~ShadowMap() = default;

	UINT Width() const;
	UINT Height() const;
	ID3D12Resource* Resource();
	CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv() const;

	D3D12_VIEWPORT Viewport() const;
	D3D12_RECT ScissorRect() const;

	void BuildDescriptors();

	void OnResize(UINT newWidth, UINT newHeight);

private:
	
	void BuildResource();

private:


	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;

	UINT mWidth = 0;
	UINT mHeight = 0;
	DXGI_FORMAT mFormat = DXGI_FORMAT_R24G8_TYPELESS;

	GMemory srvMemory;
	
	GMemory dsvMemory;

	Texture mShadowMap;

};
