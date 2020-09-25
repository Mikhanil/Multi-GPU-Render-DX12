#include "pch.h"
#include "ShadowMap.h"
#include "d3dApp.h"
#include "MemoryAllocator.h"
#include "GResourceStateTracker.h"

ShadowMap::ShadowMap(std::shared_ptr<GDevice> device, UINT width, UINT height)
{
	this->device = device;
	mWidth = width;
	mHeight = height;

	mViewport = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
	mScissorRect = {0, 0, static_cast<int>(width), static_cast<int>(height)};
		
	srvMemory = this->device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	dsvMemory = this->device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
	
	BuildResource();
}

UINT ShadowMap::Width() const
{
	return mWidth;
}

UINT ShadowMap::Height() const
{
	return mHeight;
}

GTexture& ShadowMap::GetTexture()
{
	return shadowMap;
}


D3D12_VIEWPORT ShadowMap::Viewport() const
{
	return mViewport;
}

D3D12_RECT ShadowMap::ScissorRect() const
{
	return mScissorRect;
}

void ShadowMap::OnResize(UINT newWidth, UINT newHeight)
{
	if ((mWidth != newWidth) || (mHeight != newHeight))
	{
		mWidth = newWidth;
		mHeight = newHeight;

		BuildResource();
		BuildDescriptors();
	}
}

void ShadowMap::BuildDescriptors()
{		
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.PlaneSlice = 0;
	shadowMap.CreateShaderResourceView(&srvDesc, &srvMemory);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.Texture2D.MipSlice = 0;
	shadowMap.CreateDepthStencilView(&dsvDesc, &dsvMemory);
}

void ShadowMap::BuildResource()
{
	if(shadowMap.GetD3D12Resource() == nullptr)
	{
		D3D12_RESOURCE_DESC texDesc;
		ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Alignment = 0;
		texDesc.Width = mWidth;
		texDesc.Height = mHeight;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = 1;
		texDesc.Format = mFormat;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE optClear;
		optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		optClear.DepthStencil.Depth = 1.0f;
		optClear.DepthStencil.Stencil = 0;

		shadowMap = GTexture(device, texDesc, std::wstring(L"Shadow Map"), TextureUsage::Depth, &optClear);
	}
	else
	{
		GTexture::Resize(shadowMap, mWidth, mHeight,1);
	}	
}
