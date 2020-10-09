#include "GHeap.h"


#include "d3dUtil.h"
#include "GDevice.h"

GHeap::GHeap(const std::shared_ptr<GDevice> device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
	: heapType(type)
	  , descriptorCount(numDescriptors), device(device)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
	heapDesc.Type = heapType;
	heapDesc.NumDescriptors = descriptorCount;
	heapDesc.Flags = heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
		                 ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		                 : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	heapDesc.NodeMask = device->GetNodeMask();

	ThrowIfFailed(device->GetDXDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap)));

	startCPUPtr = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	startGPUPtr = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

	descriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(heapType);
	freeHandlesCount = descriptorCount;

	AddNewBlock(0, freeHandlesCount);
}

D3D12_DESCRIPTOR_HEAP_TYPE GHeap::GetType() const
{
	return heapType;
}

uint32_t GHeap::FreeHandlerCount() const
{
	return freeHandlesCount;
}

bool GHeap::HasSpace(uint32_t numDescriptors) const
{
	return freeListBySize.lower_bound(numDescriptors) != freeListBySize.end();
}

void GHeap::AddNewBlock(uint32_t offset, uint32_t numDescriptors)
{
	const auto offsetIt = freeListByOffset.emplace(offset, numDescriptors);
	const auto sizeIt = freeListBySize.emplace(numDescriptors, offsetIt.first);
	offsetIt.first->second.FreeListBySizeIt = sizeIt;
}

GMemory GHeap::Allocate(uint32_t numDescriptors)
{
	std::lock_guard<std::mutex> lock(allocationMutex);

	if (numDescriptors > freeHandlesCount)
	{
		return GMemory();
	}

	const auto freeBlockIt = freeListBySize.lower_bound(numDescriptors);

	if (freeBlockIt == freeListBySize.end())
	{
		return GMemory();
	}

	const auto blockSize = freeBlockIt->first;

	const auto offsetIt = freeBlockIt->second;

	const auto offset = offsetIt->first;

	freeListBySize.erase(freeBlockIt);
	freeListByOffset.erase(offsetIt);

	const auto newOffset = offset + numDescriptors;
	const auto newSize = blockSize - numDescriptors;

	if (newSize > 0)
	{
		AddNewBlock(newOffset, newSize);
	}

	freeHandlesCount -= numDescriptors;

	return GMemory(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(startCPUPtr, offset, descriptorHandleIncrementSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(startGPUPtr, offset, descriptorHandleIncrementSize),
		numDescriptors, descriptorHandleIncrementSize, shared_from_this());
}

uint32_t GHeap::ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle) const
{
	return static_cast<uint32_t>(handle.ptr - startCPUPtr.ptr) / descriptorHandleIncrementSize;
}

void GHeap::Free(GMemory&& descriptor, uint64_t frameNumber)
{
	auto offset = ComputeOffset(descriptor.GetCPUHandle());

	std::lock_guard<std::mutex> lock(allocationMutex);

	staleDescriptors.emplace(offset, descriptor.GetHandlersCount(), frameNumber);
}

void GHeap::FreeBlock(uint32_t offset, uint32_t numDescriptors)
{
	const auto nextBlockIt = freeListByOffset.upper_bound(offset);

	auto prevBlockIt = nextBlockIt;

	if (prevBlockIt != freeListByOffset.begin())
	{
		--prevBlockIt;
	}
	else
	{
		prevBlockIt = freeListByOffset.end();
	}
	freeHandlesCount += numDescriptors;

	if (prevBlockIt != freeListByOffset.end() &&
		offset == prevBlockIt->first + prevBlockIt->second.Size)
	{
		offset = prevBlockIt->first;
		numDescriptors += prevBlockIt->second.Size;

		freeListBySize.erase(prevBlockIt->second.FreeListBySizeIt);
		freeListByOffset.erase(prevBlockIt);
	}

	if (nextBlockIt != freeListByOffset.end() &&
		offset + numDescriptors == nextBlockIt->first)
	{
		numDescriptors += nextBlockIt->second.Size;

		freeListBySize.erase(nextBlockIt->second.FreeListBySizeIt);
		freeListByOffset.erase(nextBlockIt);
	}

	AddNewBlock(offset, numDescriptors);
}

void GHeap::ReleaseStaleDescriptors(uint64_t frameNumber)
{
	std::lock_guard<std::mutex> lock(allocationMutex);

	while (!staleDescriptors.empty() && staleDescriptors.front().FrameNumber <= frameNumber)
	{
		auto& staleDescriptor = staleDescriptors.front();

		const auto offset = staleDescriptor.Offset;
		const auto numDescriptors = staleDescriptor.Size;

		FreeBlock(offset, numDescriptors);

		staleDescriptors.pop();
	}
}

ID3D12DescriptorHeap* GHeap::GetDescriptorHeap()
{
	return descriptorHeap.Get();
}

std::shared_ptr<GDevice> GHeap::GetDevice() const
{
	return device;
}
