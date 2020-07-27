#include "GraphicDescriptorAllocatorPage.h"
#include "d3dApp.h"

GraphicDescriptorAllocatorPage::GraphicDescriptorAllocatorPage(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
    : heapType(type)
    , numDescriptorsInHeap(numDescriptors)
{
    auto& device = DXLib::D3DApp::GetApp().GetDevice();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = heapType;
    heapDesc.NumDescriptors = numDescriptorsInHeap;

    ThrowIfFailed(device.CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&d3d12DescriptorHeap)));

    baseDescriptor = d3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    descriptorHandleIncrementSize = device.GetDescriptorHandleIncrementSize(heapType);
    freeHandlesCount = numDescriptorsInHeap;

    AddNewBlock(0, freeHandlesCount);
}

D3D12_DESCRIPTOR_HEAP_TYPE GraphicDescriptorAllocatorPage::GetType() const
{
    return heapType;
}

uint32_t GraphicDescriptorAllocatorPage::FreeHandlerCount() const
{
    return freeHandlesCount;
}

bool GraphicDescriptorAllocatorPage::HasSpace(uint32_t numDescriptors) const
{
    return freeListBySize.lower_bound(numDescriptors) != freeListBySize.end();
}

void GraphicDescriptorAllocatorPage::AddNewBlock(uint32_t offset, uint32_t numDescriptors)
{
    const auto offsetIt = freeListByOffset.emplace(offset, numDescriptors);
    const auto sizeIt = freeListBySize.emplace(numDescriptors, offsetIt.first);
    offsetIt.first->second.FreeListBySizeIt = sizeIt;
}

GraphicDescriptorAllocation GraphicDescriptorAllocatorPage::Allocate(uint32_t numDescriptors)
{
    std::lock_guard<std::mutex> lock(allocationMutex);

    // There are less than the requested number of descriptors left in the heap.
    // Return a NULL descriptor and try another heap.
    if (numDescriptors > freeHandlesCount)
    {
        return GraphicDescriptorAllocation();
    }

    // Get the first block that is large enough to satisfy the request.
    auto smallestBlockIt = freeListBySize.lower_bound(numDescriptors);
    if (smallestBlockIt == freeListBySize.end())
    {
        // There was no free block that could satisfy the request.
        return GraphicDescriptorAllocation();
    }

    // The size of the smallest block that satisfies the request.
    auto blockSize = smallestBlockIt->first;

    // The pointer to the same entry in the FreeListByOffset map.
    auto offsetIt = smallestBlockIt->second;

    // The offset in the descriptor heap.
    auto offset = offsetIt->first;

    // Remove the existing free block from the free list.
    freeListBySize.erase(smallestBlockIt);
    freeListByOffset.erase(offsetIt);

    // Compute the new free block that results from splitting this block.
    auto newOffset = offset + numDescriptors;
    auto newSize = blockSize - numDescriptors;

    if (newSize > 0)
    {
        // If the allocation didn't exactly match the requested size,
        // return the left-over to the free list.
        AddNewBlock(newOffset, newSize);
    }

    // Decrement free handles.
    freeHandlesCount -= numDescriptors;

    return GraphicDescriptorAllocation(
        CD3DX12_CPU_DESCRIPTOR_HANDLE(baseDescriptor, offset, descriptorHandleIncrementSize),
        numDescriptors, descriptorHandleIncrementSize, shared_from_this());
}

uint32_t GraphicDescriptorAllocatorPage::ComputeOffset(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
    return static_cast<uint32_t>(handle.ptr - baseDescriptor.ptr) / descriptorHandleIncrementSize;
}

void GraphicDescriptorAllocatorPage::Free(GraphicDescriptorAllocation&& descriptor, uint64_t frameNumber)
{
    // Compute the offset of the descriptor within the descriptor heap.
    auto offset = ComputeOffset(descriptor.GetDescriptorHandle());

    std::lock_guard<std::mutex> lock(allocationMutex);

    // Don't add the block directly to the free list until the frame has completed.
    descriptors.emplace(offset, descriptor.GetNumHandles(), frameNumber);
}

void GraphicDescriptorAllocatorPage::FreeBlock(uint32_t offset, uint32_t numDescriptors)
{
    // Find the first element whose offset is greater than the specified offset.
    // This is the block that should appear after the block that is being freed.
    auto nextBlockIt = freeListByOffset.upper_bound(offset);

    // Find the block that appears before the block being freed.
    auto prevBlockIt = nextBlockIt;
    // If it's not the first block in the list.
    if (prevBlockIt != freeListByOffset.begin())
    {
        // Go to the previous block in the list.
        --prevBlockIt;
    }
    else
    {
        // Otherwise, just set it to the end of the list to indicate that no
        // block comes before the one being freed.
        prevBlockIt = freeListByOffset.end();
    }

    // Add the number of free handles back to the heap.
    // This needs to be done before merging any blocks since merging
    // blocks modifies the numDescriptors variable.
    freeHandlesCount += numDescriptors;

    if (prevBlockIt != freeListByOffset.end() &&
        offset == prevBlockIt->first + prevBlockIt->second.Size)
    {
        // The previous block is exactly behind the block that is to be freed.
        //
        // PrevBlock.Offset           Offset
        // |                          |
        // |<-----PrevBlock.Size----->|<------Size-------->|
        //

        // Increase the block size by the size of merging with the previous block.
        offset = prevBlockIt->first;
        numDescriptors += prevBlockIt->second.Size;

        // Remove the previous block from the free list.
        freeListBySize.erase(prevBlockIt->second.FreeListBySizeIt);
        freeListByOffset.erase(prevBlockIt);
    }

    if (nextBlockIt != freeListByOffset.end() &&
        offset + numDescriptors == nextBlockIt->first)
    {
        // The next block is exactly in front of the block that is to be freed.
        //
        // Offset               NextBlock.Offset 
        // |                    |
        // |<------Size-------->|<-----NextBlock.Size----->|

        // Increase the block size by the size of merging with the next block.
        numDescriptors += nextBlockIt->second.Size;

        // Remove the next block from the free list.
        freeListBySize.erase(nextBlockIt->second.FreeListBySizeIt);
        freeListByOffset.erase(nextBlockIt);
    }

    // Add the freed block to the free list.
    AddNewBlock(offset, numDescriptors);
}

void GraphicDescriptorAllocatorPage::ReleaseStaleDescriptors(uint64_t frameNumber)
{
    std::lock_guard<std::mutex> lock(allocationMutex);

    while (!descriptors.empty() && descriptors.front().FrameNumber <= frameNumber)
    {
        auto& staleDescriptor = descriptors.front();

        // The offset of the descriptor in the heap.
        auto offset = staleDescriptor.Offset;
        // The number of descriptors that were allocated.
        auto numDescriptors = staleDescriptor.Size;

        FreeBlock(offset, numDescriptors);

        descriptors.pop();
    }
}
