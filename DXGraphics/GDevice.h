#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "dxgi1_6.h"
#include "Lazy.h"
#include "MemoryAllocator.h"

using namespace Microsoft::WRL;

namespace DXLib
{
	class GCommandQueue;
}

class GCrossAdapterResource;
class GResource;
class GAllocator;
class GDeviceFactory;
class GMemory;

class GDevice
{
private:
	ComPtr<ID3D12Device> device;
	ComPtr<IDXGIAdapter3> adapter;


	DXLib::Lazy<custom_vector<DXLib::Lazy<std::unique_ptr<GAllocator>>>> graphicAllocators;
	DXLib::Lazy<custom_vector<DXLib::Lazy<std::shared_ptr<DXLib::GCommandQueue>>>> queues;

	DXLib::Lazy<bool> crossAdapterTextureSupport;
	DXLib::Lazy<GResource> timestampResultBuffer;
	DXLib::Lazy<ComPtr<ID3D12QueryHeap>> timestampQueryHeap;

	std::wstring name;

	friend GCrossAdapterResource;
	friend GDeviceFactory;

	HANDLE SharedHandle(ComPtr<ID3D12DeviceChild> deviceObject, const SECURITY_ATTRIBUTES* attributes, DWORD access,
	                    LPCWSTR name) const;

	void ShareResource(GResource& resource, std::shared_ptr<GDevice> destDevice, GResource& destResource,
	                   const SECURITY_ATTRIBUTES* attributes = nullptr,
	                   DWORD access = GENERIC_ALL, LPCWSTR name = L"") const;

	void InitialDescriptorAllocator();
	void InitialCommandQueue();
	void InitialQueryTimeStamp();
	void InitialDevice();


	

public:

	GDevice(ComPtr<IDXGIAdapter3> adapter);
	
	~GDevice();

	void ResetAllocator(uint64_t frameCount);

	GMemory AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorCount = 1);

	UINT GetNodeMask() const;


	bool IsCrossAdapterTextureSupported() const;

	void SharedFence(ComPtr<ID3D12Fence>& primaryFence, std::shared_ptr<GDevice> sharedDevice,
	                 ComPtr<ID3D12Fence>& sharedFence, UINT64 fenceValue = 0,
	                 const SECURITY_ATTRIBUTES* attributes = nullptr,
	                 DWORD access = GENERIC_ALL, LPCWSTR name = L"") const;


	std::shared_ptr<DXLib::GCommandQueue> GetCommandQueue(
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	void Flush() const;

	ComPtr<ID3D12Device> GetDXDevice() const;

	std::wstring GetName() const
	{
		return name;
	}
};
