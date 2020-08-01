#include "DXAllocator.h"

#include "d3dApp.h"
#include "GAllocator.h"
#include "GDataUploader.h"

LinearAllocationStrategy<> DXAllocator::allocatorStrategy = LinearAllocationStrategy<>();

custom_vector<std::unique_ptr<GAllocator>> DXAllocator::graphicAllocator = []
{
	auto allocators = DXAllocator::CreateVector<std::unique_ptr<GAllocator>>();

	for (uint8_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		allocators.push_back(std::make_unique<GAllocator>(static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(i)));
	}
	
	return allocators;
}();

std::unique_ptr<GDataUploader> DXAllocator::uploader = std::make_unique<GDataUploader>();


void DXAllocator::ResetAllocator()
{
	uploader->Reset();
	
	const auto frameCount = DXLib::D3DApp::GetApp().GetFrameCount();
	for (uint8_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		graphicAllocator[i]->ReleaseStaleDescriptors(frameCount);
	}	
}

D3D12_GPU_VIRTUAL_ADDRESS DXAllocator::UploadData(size_t sizeInBytes, const void* bufferData, size_t alignment)
{
	const auto allocation = uploader->Allocate(sizeInBytes, alignment);
	memcpy(allocation.CPU, bufferData, sizeInBytes);
	return allocation.GPU;
}

GMemory DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorCount)
{
	return graphicAllocator[type]->Allocate(descriptorCount);
}
