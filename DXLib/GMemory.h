#pragma once
#include <cstdint>
#include <d3d12.h>
#include <memory>

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

    ID3D12DescriptorHeap* GetDescriptorHeap() const;

private:   
    void Free();
        
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor;   
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor;   
    uint32_t handlersCount;
    uint32_t descriptorSize;

    std::shared_ptr<GHeap> heap;
};

