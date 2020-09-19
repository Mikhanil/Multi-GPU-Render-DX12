#pragma once
#include "GResource.h"
#include "memory"

class GDevice;
class GResource;

class GCrossAdapterResource
{
	GResource primeResource;
	GResource sharedResource;
	
public:

	GCrossAdapterResource(D3D12_RESOURCE_DESC& desc, const std::shared_ptr<GDevice>& primeDevice,const std::shared_ptr<GDevice>
	                      & sharedDevice, const std::wstring& name= L"", const D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON, const D3D12_CLEAR_VALUE* clearValue = nullptr);

	GResource& GetPrimeResource();

	GResource& GetSharedResource();
};

