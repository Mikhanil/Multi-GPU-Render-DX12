#include "GCommandQueue.h"
#include "d3dUtil.h"
#include "GCommandList.h"
#include "GResourceStateTracker.h"
#include "pix3.h"

namespace DXLib
{
	GCommandQueue::GCommandQueue(const ComPtr<ID3D12Device2>& device, D3D12_COMMAND_LIST_TYPE type) :
		CommandListType(type)
		, device(device)
		, FenceValue(0), IsExecutorAlive(true)
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = type;
		desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 0;

		ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));
		ThrowIfFailed(device->CreateFence(FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

		switch (type)
		{
		case D3D12_COMMAND_LIST_TYPE_COPY:
			commandQueue->SetName(L"Copy Command Queue");
			break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE:
			commandQueue->SetName(L"Compute Command Queue");
			break;
		case D3D12_COMMAND_LIST_TYPE_DIRECT:
			commandQueue->SetName(L"Direct Command Queue");
			break;
		}


		CommandListExecutorThread = std::thread(&GCommandQueue::ProccessInFlightCommandLists, this);


	}

	GCommandQueue::~GCommandQueue()
	{
		IsExecutorAlive = false;
		CommandListExecutorThread.join();
	}

	uint64_t GCommandQueue::Signal()
	{
		const auto fenceValue = ++FenceValue;
		commandQueue->Signal(fence.Get(), fenceValue);
		return fenceValue;
	}

	bool GCommandQueue::IsFenceComplete(uint64_t fenceValue) const
	{
		return fence->GetCompletedValue() >= fenceValue;
	}

	void GCommandQueue::WaitForFenceValue(uint64_t fenceValue) const
	{
		if (IsFenceComplete(fenceValue))
			return;

		auto event = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		assert(event && "Failed to create fence event handle.");
		
		fence->SetEventOnCompletion(fenceValue, event);
		::WaitForSingleObject(event, DWORD_MAX);
		::CloseHandle(event);
	}

	void GCommandQueue::Flush()
	{
		std::unique_lock<std::mutex> lock(commandQueueExecutorMutex);
		CommandListExecutorCondition.wait(lock, [this] { return m_InFlightCommandLists.Empty(); });

		WaitForFenceValue(Signal());
	}
	std::shared_ptr<GCommandList> GCommandQueue::GetCommandList()
	{
		std::shared_ptr<GCommandList> commandList;

		if (m_AvailableCommandLists.Empty())
		{
			commandList = std::make_shared<GCommandList>(this->device ,this->CommandListType);
			return commandList;
		}
		
		if(m_AvailableCommandLists.TryPop(commandList))
		{
			return commandList;
		}
		return commandList;
	}

	uint64_t GCommandQueue::ExecuteCommandList(std::shared_ptr<GCommandList> commandList)
	{
		return ExecuteCommandLists(&commandList, 1);
	}

	uint64_t GCommandQueue::ExecuteCommandLists(std::shared_ptr<GCommandList>* commandLists, size_t size)
	{
		GResourceStateTracker::Lock();

		// Command lists that need to put back on the command list queue.
		std::vector<std::shared_ptr<GCommandList> > toBeQueued;
		toBeQueued.reserve(size * 2);        // 2x since each command list will have a pending command list.

		

		// Command lists that need to be executed.
		std::vector<ID3D12CommandList*> d3d12CommandLists;
		d3d12CommandLists.reserve(size * 2); // 2x since each command list will have a pending command list.

		for (size_t i =0; i < size; ++i)
		{
			auto commandList = commandLists[i];

			auto pendingCommandList = GetCommandList();
			bool hasPendingBarriers = commandList->Close(*pendingCommandList);
			pendingCommandList->Close();
			// If there are no pending barriers on the pending command list, there is no reason to 
			// execute an empty command list on the command queue.
			if (hasPendingBarriers)
			{
				d3d12CommandLists.push_back(pendingCommandList->GetGraphicsCommandList().Get());
			}
			d3d12CommandLists.push_back(commandList->GetGraphicsCommandList().Get());

			toBeQueued.push_back(pendingCommandList);
			toBeQueued.push_back(commandList);			
		}

		const UINT count = static_cast<UINT>(d3d12CommandLists.size());
		commandQueue->ExecuteCommandLists(count, d3d12CommandLists.data());
		const uint64_t fenceValue = Signal();

		GResourceStateTracker::Unlock();

		// Queue command lists for reuse.
		for (const auto commandList : toBeQueued)
		{
			m_InFlightCommandLists.Push({ fenceValue, commandList });
		}		

		return fenceValue;
	}

	void GCommandQueue::Wait(const GCommandQueue& other) const
	{
		commandQueue->Wait(other.fence.Get(), other.FenceValue);
	}
	

	ComPtr<ID3D12CommandQueue> GCommandQueue::GetD3D12CommandQueue() const
	{
		return commandQueue;
	}

	void GCommandQueue::StartPixEvent(const std::wstring message) const
	{
		PIXBeginEvent(commandQueue.Get(), 0, message.c_str());
	}

	void GCommandQueue::EndPixEvent() const
	{
		PIXEndEvent(commandQueue.Get());
	}


	void GCommandQueue::ProccessInFlightCommandLists()
	{
		std::unique_lock<std::mutex> lock(commandQueueExecutorMutex, std::defer_lock);

		while (IsExecutorAlive)
		{
			CommandListEntry commandListEntry;

			lock.lock();
			while (m_InFlightCommandLists.TryPop(commandListEntry))
			{
				
				auto fenceValue = commandListEntry.fenceValue;
				auto commandList = commandListEntry.commandList;

				WaitForFenceValue(fenceValue);

				commandList->Reset();

				m_AvailableCommandLists.Push(commandList);
			}
			lock.unlock();
			CommandListExecutorCondition.notify_one();

			std::this_thread::yield();
		}
	}
	
}
