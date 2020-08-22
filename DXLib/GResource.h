#pragma once
#include <d3d12.h>
#include <wrl.h>

#include <string>
#include <memory>

#include "d3dx12.h"

struct DescriptorHandle
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
};

class GResource
{
	static uint64_t  resourceId;
	
	
public:
	
	GResource(const std::wstring& name = L"");
	GResource(const D3D12_RESOURCE_DESC& resourceDesc,
				const std::wstring& name = L"",
	         const D3D12_CLEAR_VALUE* clearValue = nullptr, D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT));
	GResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, const std::wstring& name = L"");
	GResource(const GResource& copy);
	GResource(GResource&& move);

	GResource& operator=(const GResource& other);
	GResource& operator=(GResource&& other) noexcept;

	virtual ~GResource();

	bool IsValid() const;

	
	Microsoft::WRL::ComPtr<ID3D12Resource> GetD3D12Resource() const;

	D3D12_RESOURCE_DESC GetD3D12ResourceDesc() const;

	virtual void SetD3D12Resource(Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource,
	                              const D3D12_CLEAR_VALUE* clearValue = nullptr);

	


	
	void SetName(const std::wstring& name);

	
	
	virtual void Reset();

protected:
	uint64_t id = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> dxResource;
	std::unique_ptr<D3D12_CLEAR_VALUE> clearValue;
	std::wstring resourceName;
};
