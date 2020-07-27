#pragma once
#include "DXAllocator.h"
#include "GraphicDescriptorAllocation.h"
#include "d3dx12.h"
#include <cstdint>
#include <mutex>
#include <memory>

class GraphicDescriptorAllocatorPage;

class GraphicDescriptorAllocator
{
public:
	
    GraphicDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptorsPerPage = 256);
	
    virtual ~GraphicDescriptorAllocator();
        
    GraphicDescriptorAllocation Allocate(uint32_t numDescriptors = 1);
       
    void ReleaseStaleDescriptors(uint64_t frameNumber);

private:
    using DescriptorHeap = custom_vector< std::shared_ptr<GraphicDescriptorAllocatorPage> >;
        
    std::shared_ptr<GraphicDescriptorAllocatorPage> CreateAllocatorPage();

    D3D12_DESCRIPTOR_HEAP_TYPE allocatorType;
	
    uint32_t numDescriptorsPerPage;

    DescriptorHeap pages = DXAllocator::CreateVector<std::shared_ptr<GraphicDescriptorAllocatorPage>>();
	   
    custom_set<size_t> availablePages = DXAllocator::CreateSet<size_t>();

    std::mutex allocationMutex;
};

