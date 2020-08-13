#pragma once
#include <cstdint>
#include <d3d12.h>
#include <memory>

#include "d3dx12.h"

class GHeap;

class GMemory
{
public:
    GMemory();

    GMemory(D3D12_CPU_DESCRIPTOR_HANDLE descriptor, D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor, uint32_t handlerCount, uint32_t descriptorSize, std::shared_ptr<GHeap> heap);

    ~GMemory();
        
    GMemory(const GMemory&) = delete;
    GMemory& operator=(const GMemory&) = delete;

    GMemory(GMemory&& allocation);
    GMemory& operator=(GMemory&& other);

    bool IsNull() const;

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint32_t offset = 0) const;
	
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(uint32_t offset = 0) const;
        
    uint32_t GetHandlersCount() const;
        
    std::shared_ptr<GHeap> GetDescriptorAllocatorPage() const;

    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const;

    ID3D12DescriptorHeap* GetDescriptorHeap() const;

private:   
    void Free();
        
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuDescriptor;   
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuDescriptor;
    uint32_t handlersCount;
    uint32_t descriptorSize;

    std::shared_ptr<GHeap> heap;
};

