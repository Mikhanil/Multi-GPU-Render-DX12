#pragma once

#include <atomic>
#include <d3d12.h>  // For ID3D12CommandQueue, ID3D12Device2, and ID3D12Fence
#include <wrl.h>
#include <cstdint>
#include <memory>
#include <condition_variable> 

#include "ThreadSafeQueue.h"

class GCommandList;
class GDevice;

namespace DXLib
{
	using namespace Microsoft::WRL;

	
	
	class GCommandQueue
	{
	public:
		GCommandQueue(const std::shared_ptr<GDevice> device, D3D12_COMMAND_LIST_TYPE type);
		virtual ~GCommandQueue();

		
		std::shared_ptr<GCommandList> GetCommandList();

		uint64_t ExecuteCommandList(std::shared_ptr<GCommandList> commandList);
		uint64_t ExecuteCommandLists( std::shared_ptr<GCommandList>* commandLists, size_t size );		

		uint64_t Signal();
		void Signal(ComPtr<ID3D12Fence> otherFence, UINT64 fenceValue) const;

		bool IsFinish(uint64_t fenceValue) const;
		void WaitForFenceValue(uint64_t fenceValue) const;
		void Flush();

		void Wait(const GCommandQueue& other) const;
		void Wait(const ComPtr<ID3D12Fence> otherFence, UINT64 otherFenceValue) const;
		
		ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;

		void StartPixEvent(std::wstring message) const;

		void EndPixEvent() const;

		uint64_t GetFenceValue() const;

	private:

		// Free any command lists that are finished processing on the command queue.
		void ProccessInFlightCommandLists();
		
		// Keep track of command allocators that are "in-flight"
		struct CommandListEntry
		{
			uint64_t fenceValue;
			std::shared_ptr<GCommandList> commandList;
		};


		UINT64 queueTimestampFrequencies = 0;
		
		D3D12_COMMAND_LIST_TYPE type;
		std::shared_ptr<GDevice> device;
		ComPtr<ID3D12CommandQueue> commandQueue;
		ComPtr<ID3D12Fence> fence;
		std::atomic_uint64_t    FenceValue;

		ThreadSafeQueue<CommandListEntry>               m_InFlightCommandLists;
		ThreadSafeQueue<std::shared_ptr<GCommandList> >  m_AvailableCommandLists;


		// A thread to process in-flight command lists.
		std::thread CommandListExecutorThread;
		std::atomic_bool IsExecutorAlive;
		std::mutex commandQueueExecutorMutex;
		std::condition_variable CommandListExecutorCondition;
	};
}
