#include "GMemory.h"

#include <utility>

#include "GHeap.h"
#include "GDevice.h"
#include "GCommandQueue.h"
GMemory::GMemory()
	: cpuDescriptor{}
	  , gpuDescriptor{}
	  , handlersCount(0)
	  , descriptorSize(0)
	  , heap(nullptr)
{
}

GMemory::GMemory(D3D12_CPU_DESCRIPTOR_HANDLE descriptor, D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor,
                 uint32_t handlerCount, uint32_t descriptorSize, std::shared_ptr<GHeap> heap)
	: cpuDescriptor(descriptor)
	  , gpuDescriptor(gpuDescriptor)
	  , handlersCount(handlerCount)
	  , descriptorSize(descriptorSize)
	  , heap(std::move(heap))
{
}


GMemory::~GMemory()
{
	Free();
}

GMemory::GMemory(GMemory&& allocation)
	: cpuDescriptor(allocation.cpuDescriptor)
	  , gpuDescriptor(allocation.gpuDescriptor)
	  , handlersCount(allocation.handlersCount)
	  , descriptorSize(allocation.descriptorSize)
	  , heap(std::move(allocation.heap))
{
	allocation.cpuDescriptor.ptr = 0;
	allocation.gpuDescriptor.ptr = 0;
	allocation.handlersCount = 0;
	allocation.descriptorSize = 0;
}

GMemory& GMemory::operator=(GMemory&& other)
{
	Free();

	cpuDescriptor = other.cpuDescriptor;
	gpuDescriptor = other.gpuDescriptor;
	handlersCount = other.handlersCount;
	descriptorSize = other.descriptorSize;
	heap = std::move(other.heap);

	other.cpuDescriptor.ptr = 0;
	other.gpuDescriptor.ptr = 0;
	other.handlersCount = 0;
	other.descriptorSize = 0;

	return *this;
}

void GMemory::Free()
{
	if (!IsNull() && heap)
	{
		const auto frameValue = heap->GetDevice()->GetCommandQueue()->GetFenceValue();
		
		heap->Free(std::move(*this), frameValue);

		cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE();
		gpuDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE();
		handlersCount = 0;
		descriptorSize = 0;
		heap.reset();
	}
}

bool GMemory::IsNull() const
{
	return cpuDescriptor.ptr == 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE GMemory::GetCPUHandle(uint32_t offset) const
{
	if (offset > handlersCount)
		assert("Bad GMemmory offset");

	return CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuDescriptor, offset, descriptorSize);
}

D3D12_GPU_DESCRIPTOR_HANDLE GMemory::GetGPUHandle(uint32_t offset) const
{
	if (offset > handlersCount)
		assert("Bad GMemmory offset");

	return CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuDescriptor, offset, descriptorSize);
}

uint32_t GMemory::GetHandlersCount() const
{
	return handlersCount;
}

std::shared_ptr<GHeap> GMemory::GetDescriptorAllocatorPage() const
{
	return heap;
}

D3D12_DESCRIPTOR_HEAP_TYPE GMemory::GetType() const
{
	return heap->GetType();
}

ID3D12DescriptorHeap* GMemory::GetDescriptorHeap() const
{
	return heap->GetDescriptorHeap();
}
