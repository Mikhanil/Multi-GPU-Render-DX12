#pragma once
#include "pch.h"
#include <deque>
#include <memory>
#include <wrl.h>
#include <DXAllocator.h>
#include <d3d12.h>

using namespace Microsoft::WRL;

class GDataUploader
{
public:
	  
    struct Allocation
    {
        void* CPU;
        D3D12_GPU_VIRTUAL_ADDRESS GPU;
    };

    explicit GDataUploader(size_t pageSize = 1024*1024*2);

    virtual ~GDataUploader();

    size_t GetPageSize() const;

    
    Allocation Allocate(size_t sizeInBytes, size_t alignment);

    void Reset();

private:
   
    struct GraphicMemoryPage
    {
        GraphicMemoryPage(size_t sizeInBytes);
        ~GraphicMemoryPage();

        bool HasSpace(size_t sizeInBytes, size_t alignment) const;

        Allocation Allocate(size_t sizeInBytes, size_t alignment);

        void Reset();

    private:

        Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource;

        void* CPUPtr;
        D3D12_GPU_VIRTUAL_ADDRESS GPUPtr;

        size_t PageSize;
        size_t Offset;
    };


    custom_list<std::shared_ptr<GraphicMemoryPage>> pages = DXAllocator::CreateList<std::shared_ptr<GraphicMemoryPage>>();
	
   
    std::shared_ptr<GraphicMemoryPage> CreatePage(uint32_t pageSize) const;

   
    size_t PageSize;
};

