#pragma once

#include <d3d12.h>  // For ID3D12CommandQueue, ID3D12Device2, and ID3D12Fence
#include <wrl.h>    // For Microsoft::WRL::ComPtr

#include <cstdint>  // For uint64_t
#include <queue>    // For std::queue

namespace GameEngine
{
	namespace Graphics
	{
        using namespace Microsoft::WRL;
		
		class CommandQueue
		{
			
        public:
            CommandQueue();
            virtual ~CommandQueue() {};

            bool Initialize(Microsoft::WRL::ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
			
            // Get an available command list from the command queue.
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> GetCommandList();

            // Execute a command list.
            // Returns the fence value to wait for for this command list.
            uint64_t ExecuteCommandList(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> commandList);

            uint64_t Signal();
            bool IsFenceComplete(uint64_t fenceValue);
            void WaitForFenceValue(uint64_t fenceValue);
            void Flush();

            Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;
			
        protected:
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CreateCommandAllocator();
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> CreateCommandList(Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator);

        private:
            // Keep track of command allocators that are "in-flight"
            struct CommandAllocatorEntry
            {
                uint64_t fenceValue;
                Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
            };



			
            D3D12_COMMAND_LIST_TYPE commandListType;
            Microsoft::WRL::ComPtr<ID3D12Device2> device;
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
            Microsoft::WRL::ComPtr<ID3D12Fence> fence;
            HANDLE fenceEvent;
            uint64_t fenceValue;

            std::queue<CommandAllocatorEntry> commandAllocatorQueue;
            std::queue<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2>> commandListQueue;

		};
	}
}



