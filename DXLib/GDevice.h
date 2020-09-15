#pragma once
#include "d3dUtil.h"


namespace DXLib
{
	class GCommandQueue;
}

class GDevice
{
private:
	ComPtr<ID3D12Device2> device;
	ComPtr<IDXGIAdapter3> adapter;

	std::shared_ptr<DXLib::GCommandQueue> directCommandQueue;
	std::shared_ptr<DXLib::GCommandQueue> computeCommandQueue;
	std::shared_ptr<DXLib::GCommandQueue> copyCommandQueue;
	
public:
	GDevice(ComPtr<IDXGIAdapter3> adapter);

	std::shared_ptr<DXLib::GCommandQueue> GetCommandQueue(
		D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

	UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

	void Flush() const;
	
	ComPtr<ID3D12Device2> GetDevice() const;
	~GDevice();
};

