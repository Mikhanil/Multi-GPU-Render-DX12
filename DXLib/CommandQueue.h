#pragma once

#include <d3d12.h>  // For ID3D12CommandQueue, ID3D12Device2, and ID3D12Fence
#include <wrl.h>    
#include <cstdint>
#include <queue>

namespace DXLib
{
	using namespace Microsoft::WRL;

	
	class CommandQueue
	{

    public:
        CommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
        virtual ~CommandQueue();

        // Get an available command list from the command queue.
        ComPtr<ID3D12GraphicsCommandList2> GetCommandList();

        // Execute a command list.
        // Returns the fence value to wait for for this command list.
        uint64_t ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList2> commandList);

        uint64_t Signal();
		
        bool IsFenceComplete(uint64_t fenceValue) const;
        void WaitForFenceValue(uint64_t fenceValue) const;
        void Flush();

        ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;
		
    protected:

        ComPtr<ID3D12CommandAllocator> CreateCommandAllocator() const;
        ComPtr<ID3D12GraphicsCommandList2> CreateCommandList(ComPtr<ID3D12CommandAllocator> allocator) const;

    private:

        // Keep track of command allocators that are "in-flight"
        struct CommandAllocatorEntry
        {
	        uint64_t fenceValue;
	        ComPtr<ID3D12CommandAllocator> commandAllocator;
        };


        D3D12_COMMAND_LIST_TYPE CommandListType;
        ComPtr<ID3D12Device2> d3d12Device;
        ComPtr<ID3D12CommandQueue> d3d12CommandQueue;
        ComPtr<ID3D12Fence> d3d12Fence;
        HANDLE FenceEvent;
        uint64_t FenceValue;

        std::queue<CommandAllocatorEntry> CommandAllocatorQueue;
        std::queue<ComPtr<ID3D12GraphicsCommandList2>> CommandListQueue;
		
	};
	
}



