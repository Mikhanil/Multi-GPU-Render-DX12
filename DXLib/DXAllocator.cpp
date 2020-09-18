#include "DXAllocator.h"

#include "d3dApp.h"
#include "GAllocator.h"
#include "GDataUploader.h"
#include "GDevice.h"

LinearAllocationStrategy<> DXAllocator::allocatorStrategy = LinearAllocationStrategy<>();

static custom_unordered_map<GraphicsAdapter, DXLib::Lazy< custom_vector<DXLib::Lazy<std::unique_ptr<GAllocator>>>>> CreateAllocators()
{
	auto allocators = DXAllocator::CreateUnorderedMap < GraphicsAdapter, DXLib::Lazy< custom_vector<DXLib::Lazy<std::unique_ptr<GAllocator>>>>>();

for (UINT adapterIndex = 0; adapterIndex < GraphicsAdapter::GraphicsAdaptersCount; ++adapterIndex)
	{
		//замыкание
		auto aIndex = adapterIndex;

		const auto lazyAdapterAllocators = DXLib::Lazy<custom_vector<DXLib::Lazy<std::unique_ptr<GAllocator>>>>([aIndex]
		{
			auto device = GDevice::GetDevice(GraphicsAdapter(aIndex));

			auto adapterAllocators = DXAllocator::CreateVector<DXLib::Lazy<std::unique_ptr<GAllocator>>>();
			for (uint8_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
			{
				auto type = i;
				adapterAllocators.push_back(DXLib::Lazy<std::unique_ptr<GAllocator>>([type, device]
				{
					return std::make_unique<GAllocator>(device, static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(type));
				}));
			}

			return adapterAllocators;
		});
		

		allocators[GraphicsAdapter(adapterIndex)] = lazyAdapterAllocators;
	}

	return allocators;	
}


custom_unordered_map<GraphicsAdapter, DXLib::Lazy< custom_vector<DXLib::Lazy<std::unique_ptr<GAllocator>>>>> DXAllocator::graphicAllocators = CreateAllocators();



void DXAllocator::ResetAllocator()
{	
	const auto frameCount = DXLib::D3DApp::GetApp().GetFrameCount();

	for (auto&& element : graphicAllocators)
	{
		if(element.second.IsInit())
		{
			for (uint8_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
			{
				if(element.second.value()[i].IsInit())
					element.second.value()[i].value()->ReleaseStaleDescriptors(frameCount);
			}
		}		
	}	
}



GMemory DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorCount, GraphicsAdapter adapter )
{
	auto allocators = graphicAllocators[adapter];	
	return allocators.value()[type].value()->Allocate(descriptorCount);
}
