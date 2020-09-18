#pragma once
#include "d3dUtil.h"
#include "Lazy.h"
#include "DXAllocator.h"
#include "GResource.h"

namespace DXLib
{
	class GCommandQueue;
}


class GResource;

class GDevice
{
private:
	ComPtr<ID3D12Device2> device;
	ComPtr<IDXGIAdapter3> adapter;
			
	DXLib::Lazy< std::shared_ptr<DXLib::GCommandQueue>> directCommandQueue;
	DXLib::Lazy< std::shared_ptr<DXLib::GCommandQueue>> computeCommandQueue;
	DXLib::Lazy< std::shared_ptr<DXLib::GCommandQueue>> copyCommandQueue;

	static ComPtr<IDXGIFactory4> CreateFactory();
	static ComPtr<IDXGIFactory4> dxgiFactory;

	static custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> CreateDevices();
	static custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> gdevices;

	static custom_vector<ComPtr<IDXGIAdapter3>> CreateAdapters();
	static custom_vector<ComPtr<IDXGIAdapter3>> adapters;

	DXLib::Lazy<bool> crossAdapterTextureSupport;
	DXLib::Lazy<GResource> timestampResultBuffer;
	DXLib::Lazy<ComPtr<ID3D12QueryHeap>> timestampQueryHeap;
	
public:

	bool IsCrossAdapterTextureSupported();

	static ComPtr<IDXGIFactory4> GetFactory();

	static std::shared_ptr<GDevice> GetDevice(GraphicsAdapter adapter = GraphicsAdapter::Primary);


	void InitialQueue();
	GDevice(ComPtr<IDXGIAdapter3> adapter);

	std::shared_ptr<DXLib::GCommandQueue> GetCommandQueue(
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	void Flush() const;
	
	ComPtr<ID3D12Device2> GetDXDevice() const;
	
	~GDevice();
};

