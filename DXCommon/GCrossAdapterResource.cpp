#include "pch.h"
#include "GCrossAdapterResource.h"
#include "GDevice.h"
#include "GResource.h"

GCrossAdapterResource::GCrossAdapterResource(D3D12_RESOURCE_DESC& desc, const std::shared_ptr<GDevice>& primeDevice,
                                             const std::shared_ptr<GDevice>& sharedDevice, const std::wstring& name,
                                             const D3D12_RESOURCE_STATES initialState,
                                             const D3D12_CLEAR_VALUE* clearValue)
{

	desc.Flags = primeDevice->IsCrossAdapterTextureSupported() ? desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER : D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		
	primeResource = std::make_shared<GResource>(primeDevice, desc, name, clearValue, initialState, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER);

	sharedResource = std::make_shared<GResource>(name + L" Shared");

	primeDevice->ShareResource(*primeResource, sharedDevice, *sharedResource);
}

const GResource& GCrossAdapterResource::GetPrimeResource() const
{
	return *primeResource;
}

const GResource& GCrossAdapterResource::GetSharedResource() const
{
	return *sharedResource;
}
