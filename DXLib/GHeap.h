#pragma once
#include "DXAllocator.h"
#include <memory>
#include <d3d12.h>
#include "GMemory.h"
#include <wrl/client.h>
#include "d3dx12.h"
#include <mutex>

class GHeap : public std::enable_shared_from_this<GHeap>
{
public:
    GHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);

    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const;
   
    bool HasSpace(uint32_t numDescriptors) const;
       
    uint32_t FreeHandlerCount() const;

    GMemory Allocate(uint32_t numDescriptors);
        
    void Free(GMemory&& descriptorHandle, uint64_t frameNumber);
       
    void ReleaseStaleDescriptors(uint64_t frameNumber);

    ID3D12DescriptorHeap* GetDescriptorHeap();
protected:
        
    uint32_t ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;

    void AddNewBlock(uint32_t offset, uint32_t numDescriptors);

    void FreeBlock(uint32_t offset, uint32_t numDescriptors);

private:
    using OffsetType = size_t;
   
    using SizeType = size_t;

    struct FreeBlockInfo;
	    
        

    struct FreeBlockInfo
    {
        FreeBlockInfo(SizeType size)
            : Size(size)
        {}

        SizeType Size;
        custom_multimap<SizeType, custom_map<OffsetType, FreeBlockInfo>::iterator>::iterator FreeListBySizeIt;
    };

    struct DescriptorInfo
    {
        DescriptorInfo(OffsetType offset, SizeType size, uint64_t frame)
            : Offset(offset)
            , Size(size)
            , FrameNumber(frame)
        {}

        OffsetType Offset;
        SizeType Size;
        uint64_t FrameNumber;
    };
    
    custom_map<OffsetType, FreeBlockInfo> freeListByOffset = DXAllocator::CreateMap<OffsetType, FreeBlockInfo>();
    custom_multimap<SizeType, custom_map<OffsetType, FreeBlockInfo>::iterator> freeListBySize = DXAllocator::CreateMultimap<SizeType, custom_map<OffsetType, FreeBlockInfo>::iterator>();
	
    custom_queue<DescriptorInfo> staleDescriptors = DXAllocator::CreateQueue<DescriptorInfo>();

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> d3d12DescriptorHeap;
	
    D3D12_DESCRIPTOR_HEAP_TYPE heapType;
    CD3DX12_CPU_DESCRIPTOR_HANDLE startCPUDescriptor;    	
    CD3DX12_GPU_DESCRIPTOR_HANDLE startGPUDescriptor;
	
    uint32_t descriptorHandleIncrementSize;
    uint32_t numDescriptorsInHeap;
    uint32_t freeHandlesCount;

    std::mutex allocationMutex;
};

