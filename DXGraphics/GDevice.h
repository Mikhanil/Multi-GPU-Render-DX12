#pragma once
#include "d3dApp.h"
#include "d3dUtil.h"
#include "Lazy.h"
#include "MemoryAllocator.h"
#include "GResource.h"

namespace DXLib
{
	class GCommandQueue;
}

class GCrossAdapterResource;
class GResource;
class GAllocator;

class GDevice
{
private:
	ComPtr<ID3D12Device2> device;
	ComPtr<IDXGIAdapter3> adapter;

	DXLib::Lazy<custom_vector<DXLib::Lazy<std::shared_ptr<DXLib::GCommandQueue>>>> queues;

	static ComPtr<IDXGIFactory4> CreateFactory();
	static ComPtr<IDXGIFactory4> dxgiFactory;

	static bool CheckTearingSupport();
	static DXLib::Lazy<bool> isTearingSupport;

	static custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> CreateDevices();
	static custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> devices;

	static custom_vector<ComPtr<IDXGIAdapter3>> CreateAdapters();
	static custom_vector<ComPtr<IDXGIAdapter3>> adapters;

	DXLib::Lazy<bool> crossAdapterTextureSupport;
	DXLib::Lazy<GResource> timestampResultBuffer;
	DXLib::Lazy<ComPtr<ID3D12QueryHeap>> timestampQueryHeap;

	std::wstring name;
	UINT nodeMask = 0;

	friend GCrossAdapterResource;

	HANDLE SharedHandle(ComPtr<ID3D12DeviceChild> deviceObject, const SECURITY_ATTRIBUTES* attributes, DWORD access,
	                    LPCWSTR name) const;

	void ShareResource(GResource& resource, std::shared_ptr<GDevice> destDevice, GResource& destResource,
	                   const SECURITY_ATTRIBUTES* attributes = nullptr,
	                   DWORD access = GENERIC_ALL, LPCWSTR name = L"") const;

	void InitialDescriptorAllocator();
	void InitialCommandQueue();
	void InitialQueryTimeStamp();


	void InitialDevice();

	DXLib::Lazy<custom_vector<DXLib::Lazy<std::unique_ptr<GAllocator>>>> graphicAllocators;
public:

	GDevice(ComPtr<IDXGIAdapter3> adapter, UINT nodeMask);

	~GDevice();

	void ResetAllocator(uint64_t frameCount);

	GMemory AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorCount = 1);

	UINT GetNodeMask() const;

	static bool IsTearingSupport();

	bool IsCrossAdapterTextureSupported();

	void SharedFence(ComPtr<ID3D12Fence> primaryFence, std::shared_ptr<GDevice> sharedDevice,
	                 ComPtr<ID3D12Fence> sharedFence, UINT64 fenceValue = 0,
	                 const SECURITY_ATTRIBUTES* attributes = nullptr,
	                 DWORD access = GENERIC_ALL, LPCWSTR name = L"") const;

	ComPtr<IDXGISwapChain4> CreateSwapChain(DXGI_SWAP_CHAIN_DESC1& desc, HWND hwnd) const;

	std::shared_ptr<DXLib::GCommandQueue> GetCommandQueue(
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	void Flush() const;

	ComPtr<ID3D12Device2> GetDXDevice() const;

	static ComPtr<IDXGIFactory4> GetFactory();

	static std::shared_ptr<GDevice> GetDevice(GraphicsAdapter adapter = GraphicAdapterPrimary);
};
