#pragma once
#include "pch.h"
#include <deque>
#include <memory>
#include <wrl.h>
#include <DXAllocator.h>
#include <d3d12.h>

using namespace Microsoft::WRL;


struct UploadAllocation
{
    BYTE* CPU;
    D3D12_GPU_VIRTUAL_ADDRESS GPU;
    ID3D12Resource& d3d12Resource;
    size_t Offset;
};


class GDataUploader
{
public:

    
   

    explicit GDataUploader(size_t pageSize = 1024);

    virtual ~GDataUploader();

    size_t GetPageSize() const;
    
    UploadAllocation Allocate(size_t sizeInBytes, size_t alignment);

    void Reset();
    void Clear();

private:
   
    struct UploadMemoryPage
    {
        UploadMemoryPage(size_t sizeInBytes);
        ~UploadMemoryPage();

        bool HasSpace(size_t sizeInBytes, size_t alignment) const;

        UploadAllocation Allocate(size_t sizeInBytes, size_t alignment);

        void Reset();

    private:

        Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource;
           	
        BYTE* CPUPtr;
        D3D12_GPU_VIRTUAL_ADDRESS GPUPtr;

        size_t PageSize;
        size_t Offset;
    };


    custom_list<std::shared_ptr<UploadMemoryPage>> pages = DXAllocator::CreateList<std::shared_ptr<UploadMemoryPage>>();
	
   
    std::shared_ptr<UploadMemoryPage> CreatePage(uint32_t pageSize) const;

   
    size_t PageSize;
};

