#include "CommandQueue.h"

#include "d3dUtil.h"

namespace DXLib
{
	CommandQueue::CommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type) : CommandListType(type)
	                                                                                         , d3d12Device(device)
	                                                                                         , FenceValue(0)
	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = type;
		desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 0;

		ThrowIfFailed(d3d12Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));
		ThrowIfFailed(d3d12Device->CreateFence(FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&d3d12Fence)));

		FenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
		assert(FenceEvent && "Failed to create fence event handle.");
	}

	CommandQueue::~CommandQueue()
	{
	}

	uint64_t CommandQueue::Signal()
	{
		const auto fenceValue = ++FenceValue;
		d3d12CommandQueue->Signal(d3d12Fence.Get(), fenceValue);
		return fenceValue;
	}

	bool CommandQueue::IsFenceComplete(uint64_t fenceValue) const
	{
		return d3d12Fence->GetCompletedValue() >= fenceValue;
	}

	void CommandQueue::WaitForFenceValue(uint64_t fenceValue) const
	{
		if (IsFenceComplete(fenceValue))
			return;

		d3d12Fence->SetEventOnCompletion(fenceValue, FenceEvent);
		WaitForSingleObject(FenceEvent, DWORD_MAX);
	}

	void CommandQueue::Flush()
	{
		WaitForFenceValue(Signal());
	}

	ComPtr<ID3D12CommandAllocator> CommandQueue::CreateCommandAllocator() const
	{
		ComPtr<ID3D12CommandAllocator> commandAllocator;
		ThrowIfFailed(d3d12Device->CreateCommandAllocator(CommandListType, IID_PPV_ARGS(&commandAllocator)));
		return commandAllocator;
	}

	ComPtr<ID3D12GraphicsCommandList2> CommandQueue::CreateCommandList(
		const ComPtr<ID3D12CommandAllocator> allocator) const
	{
		ComPtr<ID3D12GraphicsCommandList2> commandList;
		ThrowIfFailed(
			d3d12Device->CreateCommandList(0, CommandListType, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));

		return commandList;
	}

	ComPtr<ID3D12GraphicsCommandList2> CommandQueue::GetCommandList()
	{
		ComPtr<ID3D12CommandAllocator> commandAllocator;
		ComPtr<ID3D12GraphicsCommandList2> commandList;

		/*Проверяем можем ли мы переиспользовать уже созданный алокатор команд*/
		if (CommandAllocatorQueue.empty() || !IsFenceComplete(CommandAllocatorQueue.front().fenceValue))
		{
			commandAllocator = CreateCommandAllocator();
		}
		else
		{
			commandAllocator = CommandAllocatorQueue.front().commandAllocator;
			CommandAllocatorQueue.pop();

			ThrowIfFailed(commandAllocator->Reset());
		}


		/*Переиспользование списков команд*/
		if (CommandListQueue.empty())
		{
			commandList = CreateCommandList(commandAllocator);
		}
		else
		{
			commandList = CommandListQueue.front();
			CommandListQueue.pop();

			ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
		}


		//Связываем лист команд и аллокатор. Чтобы можно было получить алокатор из списка
		ThrowIfFailed(commandList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), commandAllocator.Get()));

		return commandList;
	}

	uint64_t CommandQueue::ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList2> commandList)
	{
		commandList->Close();

		ID3D12CommandAllocator* commandAllocator;
		UINT dataSize = sizeof(commandAllocator);
		ThrowIfFailed(commandList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator));

		ID3D12CommandList* const ppCommandLists[] = {
			commandList.Get()
		};

		d3d12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
		const auto fenceValue = Signal();

		CommandAllocatorQueue.emplace(CommandAllocatorEntry{fenceValue, commandAllocator});
		CommandListQueue.push(commandList);


		//Удаляем временный указатель
		commandAllocator->Release();

		return fenceValue;
	}

	ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const
	{
		return d3d12CommandQueue;
	}
}
