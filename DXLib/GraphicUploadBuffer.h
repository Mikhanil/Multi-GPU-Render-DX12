#pragma once
#include "pch.h"
#include <deque>
#include <memory>
#include <wrl.h>
#include <DXAllocator.h>
#include <d3d12.h>

using namespace Microsoft::WRL;

class GraphicUploadBuffer
{
public:
	
    // Use to upload data to the GPU
    struct Allocation
    {
        void* CPU;
        D3D12_GPU_VIRTUAL_ADDRESS GPU;
    };

    /**
     * @param pageSize The size to use to allocate new pages in GPU memory.
     */
    explicit GraphicUploadBuffer(size_t pageSize = 1024*1024*2);

    virtual ~GraphicUploadBuffer();

    /**
     * The maximum size of an allocation is the size of a single page.
     */
    size_t GetPageSize() const;

    /**
     * Allocate memory in an Upload heap.
     * An allocation must not exceed the size of a page.
     * Use a memcpy or similar method to copy the
     * buffer data to CPU pointer in the Allocation structure returned from
     * this function.
     */
    Allocation Allocate(size_t sizeInBytes, size_t alignment);

    /**
     * Release all allocated pages. This should only be done when the command list
     * is finished executing on the CommandQueue.
     */
    void Reset();

private:
    // A single page for the allocator.
    struct GraphicMemoryPage
    {
        GraphicMemoryPage(size_t sizeInBytes);
        ~GraphicMemoryPage();

        // Check to see if the page has room to satisfy the requested
        // allocation.
        bool HasSpace(size_t sizeInBytes, size_t alignment) const;

        // Allocate memory from the page.
        // Throws std::bad_alloc if the the allocation size is larger
        // that the page size or the size of the allocation exceeds the 
        // remaining space in the page.
        Allocation Allocate(size_t sizeInBytes, size_t alignment);

        // Reset the page for reuse.
        void Reset();

    private:

        Microsoft::WRL::ComPtr<ID3D12Resource> d3d12Resource;

        // Base pointer.
        void* CPUPtr;
        D3D12_GPU_VIRTUAL_ADDRESS GPUPtr;

        // Allocated page size.
        size_t PageSize;
        // Current allocation offset in bytes.
        size_t Offset;
    };


    custom_list<std::shared_ptr<GraphicMemoryPage>> pages = DXAllocator::CreateList<std::shared_ptr<GraphicMemoryPage>>();
	
    //// A pool of memory pages.
    //using PagePool = custom_deque<std::shared_ptr<Page>>;

    // Request a page from the pool of available pages
    // or create a new page if there are no available pages.
    std::shared_ptr<GraphicMemoryPage> CreatePage(uint32_t pageSize) const;

    //PagePool PagePools = DXAllocator::CreateDeque<std::shared_ptr<Page>>();
    //PagePool AvailablePages = DXAllocator::CreateDeque<std::shared_ptr<Page>>();

    //std::shared_ptr<Page> CurrentPage;

    // The size of each page of memory.
    size_t PageSize;
};

