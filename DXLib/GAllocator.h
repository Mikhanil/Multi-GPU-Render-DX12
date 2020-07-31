#pragma once
#include "DXAllocator.h"
#include "GMemory.h"
#include "d3dx12.h"
#include <cstdint>
#include <mutex>
#include <memory>

class GHeap;

class GAllocator
{
public:
	
    GAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorsPerPage = 1024);
	
    virtual ~GAllocator();
        
    GMemory Allocate(uint32_t descriptorCount = 1);
       
    void ReleaseStaleDescriptors(uint64_t frameNumber);

private:
    using GraphicMemoryPage = custom_vector< std::shared_ptr<GHeap> >;
        
    std::shared_ptr<GHeap> CreateAllocatorPage();

    D3D12_DESCRIPTOR_HEAP_TYPE allocatorType;
	
    uint32_t numDescriptorsPerPage;

    GraphicMemoryPage pages = DXAllocator::CreateVector<std::shared_ptr<GHeap>>();
	   
    custom_set<size_t> availablePages = DXAllocator::CreateSet<size_t>();

    std::mutex allocationMutex;
};

