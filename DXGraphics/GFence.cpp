#include "GFence.h"
#include "GDevice.h"
#include "GCommandQueue.h"

GFence::GFence(const std::shared_ptr<GDevice> device, D3D12_FENCE_FLAGS flags): FenceValue(0), device(device)
{
	device->GetDXDevice()->CreateFence(0, flags, IID_PPV_ARGS(&fence));
}

ComPtr<ID3D12Fence> GFence::GetDXFence() const
{
	return fence;
}

void GFence::Signal(std::shared_ptr<DXLib::GCommandQueue> queue)
{
	const auto fenceValue = ++FenceValue;
	queue->GetD3D12CommandQueue()->Signal(fence.Get(), fenceValue);
}

UINT64 GFence::GetFenceValue() const
{
	return FenceValue;
}

bool GFence::IsFenceComplete(uint64_t fenceValue) const
{
	return fence->GetCompletedValue() >= fenceValue;
}

void GFence::WaitForFenceValue(uint64_t fenceValue) const
{
	if (IsFenceComplete(fenceValue))
		return;

	auto event = ::CreateEvent(nullptr, FALSE, FALSE, NULL);
	assert(event && "Failed to create fence event handle.");

	fence->SetEventOnCompletion(fenceValue, event);
	WaitForSingleObject(event, DWORD_MAX);
	CloseHandle(event);
}
