#pragma once

#include <atomic>
#include <d3d12.h>  // For ID3D12CommandQueue, ID3D12Device2, and ID3D12Fence
#include <wrl.h>
#include <cstdint>
#include <memory>
#include <condition_variable>


#include "Lazy.h"
#include "LockThreadQueue.h"

class GCommandList;
class GDevice;
class GResource;

namespace DXLib
{
	using namespace Microsoft::WRL;


	class GCommandQueue : public std::enable_shared_from_this<GCommandQueue>
	{
	public:
		GCommandQueue(std::shared_ptr<GDevice> device, D3D12_COMMAND_LIST_TYPE type);
		virtual ~GCommandQueue();


		std::shared_ptr<GCommandList> GetCommandList();

		uint64_t ExecuteCommandList(std::shared_ptr<GCommandList> commandList);
		uint64_t ExecuteCommandLists(std::shared_ptr<GCommandList>* commandLists, size_t size);

		uint64_t Signal();
		void Signal(ComPtr<ID3D12Fence> otherFence, UINT64 fenceValue) const;

		bool IsFinish(uint64_t fenceValue) const;
		void WaitForFenceValue(uint64_t fenceValue) const;
		void Flush();

		void Wait(const GCommandQueue& other) const;
		void Wait(ComPtr<ID3D12Fence> otherFence, UINT64 otherFenceValue) const;

		ComPtr<ID3D12CommandQueue> GetD3D12CommandQueue() const;

		void StartPixEvent(std::wstring message) const;

		void EndPixEvent() const;

		uint64_t GetFenceValue() const;

		UINT64 GetTimestampFreq();

		UINT64 GetTimestamp(UINT index);

	private:

		friend class GDevice;
		friend class GCommandList;

		void HardStop();

		// Free any command lists that are finished processing on the command queue.
		void ProccessInFlightCommandLists();

		// Keep track of command allocators that are "in-flight"
		struct CommandListEntry
		{
			uint64_t fenceValue;
			std::shared_ptr<GCommandList> commandList;
		};

		// Get the timestamp values from the result buffers.
		D3D12_RANGE readRange = {};
		const D3D12_RANGE emptyRange = {};

		void* mappedData = nullptr;
		Lazy<GResource> timestampResultBuffer;
		Lazy<ComPtr<ID3D12QueryHeap>> timestampQueryHeap;


		UINT64 queueTimestampFrequencies = 0;
		LARGE_INTEGER cpuTimestampFrequencies;

		D3D12_COMMAND_LIST_TYPE type;
		std::shared_ptr<GDevice> device;
		ComPtr<ID3D12CommandQueue> commandQueue;
		ComPtr<ID3D12Fence> fence;
		std::atomic_uint64_t FenceValue;

		LockThreadQueue<CommandListEntry> m_InFlightCommandLists;
		LockThreadQueue<std::shared_ptr<GCommandList>> availableCommandLists;


		// A thread to process in-flight command lists.
		std::thread CommandListExecutorThread;
		std::atomic_bool IsExecutorAlive;
		std::mutex commandQueueExecutorMutex;
		std::condition_variable CommandListExecutorCondition;
	};
}
