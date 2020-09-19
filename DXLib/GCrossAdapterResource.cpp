#include "GCrossAdapterResource.h"

#include "GDevice.h"
#include "GResource.h"

GCrossAdapterResource::GCrossAdapterResource(D3D12_RESOURCE_DESC& desc, const std::shared_ptr<GDevice>& primeDevice,const std::shared_ptr<GDevice>& sharedDevice, const std::wstring& name, const D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue)
{   
    desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	primeResource = GResource(primeDevice, desc, name, clearValue, initialState);

	sharedResource = GResource(name + L" Shared");

	primeDevice->ShareResource(primeResource, sharedDevice, sharedResource);
		
}

GResource& GCrossAdapterResource::GetPrimeResource()
{
	return primeResource;
}

GResource& GCrossAdapterResource::GetSharedResource()
{
	return sharedResource;
}

