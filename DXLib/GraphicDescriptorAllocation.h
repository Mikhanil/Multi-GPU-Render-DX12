#pragma once
#include <cstdint>
#include <d3d12.h>
#include <memory>

class GraphicDescriptorAllocatorPage;

class GraphicDescriptorAllocation
{
public:
    // Creates a NULL descriptor.
    GraphicDescriptorAllocation();

    GraphicDescriptorAllocation(D3D12_CPU_DESCRIPTOR_HANDLE descriptor, uint32_t numHandles, uint32_t descriptorSize, std::shared_ptr<GraphicDescriptorAllocatorPage> page);

    // The destructor will automatically free the allocation.
    ~GraphicDescriptorAllocation();

    // Copies are not allowed.
    GraphicDescriptorAllocation(const GraphicDescriptorAllocation&) = delete;
    GraphicDescriptorAllocation& operator=(const GraphicDescriptorAllocation&) = delete;

    // Move is allowed.
    GraphicDescriptorAllocation(GraphicDescriptorAllocation&& allocation);
    GraphicDescriptorAllocation& operator=(GraphicDescriptorAllocation&& other);

    // Check if this a valid descriptor.
    bool IsNull() const;

    // Get a descriptor at a particular offset in the allocation.
    D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptorHandle(uint32_t offset = 0) const;

    // Get the number of (consecutive) handles for this allocation.
    uint32_t GetNumHandles() const;

    // Get the heap that this allocation came from.
    // (For internal use only).
    std::shared_ptr<GraphicDescriptorAllocatorPage> GetDescriptorAllocatorPage() const;

private:
    // Free the descriptor back to the heap it came from.
    void Free();

    // The base descriptor.
    D3D12_CPU_DESCRIPTOR_HANDLE m_Descriptor;
    // The number of descriptors in this allocation.
    uint32_t m_NumHandles;
    // The offset to the next descriptor.
    uint32_t m_DescriptorSize;

    // A pointer back to the original page where this allocation came from.
    std::shared_ptr<GraphicDescriptorAllocatorPage> m_Page;
};

