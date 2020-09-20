#pragma once
#include <atomic>
#include <d3d12.h>  // For ID3D12CommandQueue, ID3D12Device2, and ID3D12Fence
#include <memory>
#include <wrl.h>
using namespace Microsoft::WRL;

class GDevice;

namespace DXLib
{
	class GCommandQueue;
}


class GFence
{
	ComPtr<ID3D12Fence> fence;
	std::atomic_uint64_t FenceValue;
	std::shared_ptr<GDevice> device;
public:

	GFence(std::shared_ptr<GDevice> device, D3D12_FENCE_FLAGS flags = D3D12_FENCE_FLAG_NONE);


	ComPtr<ID3D12Fence> GetDXFence() const;
	void Signal(std::shared_ptr<DXLib::GCommandQueue> queue);;

	UINT64 GetFenceValue() const;


	bool IsFenceComplete(uint64_t fenceValue) const;
	void WaitForFenceValue(uint64_t fenceValue) const;
};
