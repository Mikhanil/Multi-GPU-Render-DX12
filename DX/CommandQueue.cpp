#include "CommandQueue.h"
#include "COMException.h"
#include "ErrorLogger.h"

namespace GameEngine
{
	namespace Graphics
	{
		CommandQueue::CommandQueue():fenceValue(0)
		{  
		}

		bool CommandQueue::Initialize(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
		{
			this->device = device;
			this->commandListType = type;

			try
			{
				D3D12_COMMAND_QUEUE_DESC desc = {};
				ZeroMemory(&desc, sizeof(desc));
				desc.Type = commandListType;
				desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
				desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				desc.NodeMask = 0;
				
				auto result = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue));
				CheckComError(result, "Failed to create command queue");

				result = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));				

				fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);

				
			}
			catch (COMException & ex)
			{
				Logger::ErrorLogger::Log(ex);
				return false;
			}

			return  true;
		}

		ComPtr<ID3D12CommandAllocator> CommandQueue::CreateCommandAllocator()
		{
			try
			{
				ComPtr<ID3D12CommandAllocator> commandAllocator;

				auto result = device->CreateCommandAllocator(commandListType, IID_PPV_ARGS(&commandAllocator));
				CheckComError(result, "Failed to create command allocator");


				return commandAllocator;
			}
			catch (COMException& ex)
			{
				Logger::ErrorLogger::Log(ex);
			}
		}

		ComPtr<ID3D12GraphicsCommandList2> CommandQueue::CreateCommandList(ComPtr<ID3D12CommandAllocator> allocator)
		{
				ComPtr<ID3D12GraphicsCommandList2> commandList;
				auto result = device->CreateCommandList(0,  commandListType, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
				CheckComError(result, "Failed to create graphics command list");
				return commandList;
		}

		ComPtr<ID3D12GraphicsCommandList2> CommandQueue::GetCommandList()
		{
				ComPtr<ID3D12CommandAllocator> commandAllocator;
				ComPtr<ID3D12GraphicsCommandList2> commandList;

				if (!commandAllocatorQueue.empty() && IsFenceComplete(commandAllocatorQueue.front().fenceValue))
				{
					commandAllocator = commandAllocatorQueue.front().commandAllocator;
					commandAllocatorQueue.pop();

					auto result = (commandAllocator->Reset());
					CheckComError(result, "Failed to reset");
					
				}
				else
				{
					commandAllocator = CreateCommandAllocator();
				}

				if (!commandListQueue.empty())
				{
					commandList = commandListQueue.front();
					commandListQueue.pop();

					auto result = (commandList->Reset(commandAllocator.Get(), nullptr));
					CheckComError(result, "Failed to reset");
				}
				else
				{
					commandList = CreateCommandList(commandAllocator);
				}
				// Associate the command allocator with the command list so that it can be
				// retrieved when the command list is executed.
				auto result = (commandList->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), commandAllocator.Get()));
				CheckComError(result, "Failed to SetPrivateDataInterface");
				
				return commandList;
		}

		uint64_t CommandQueue::Signal()
		{
			uint64_t fenceValue = ++ this->fenceValue;
			commandQueue->Signal(fence.Get(), fenceValue);
			return fenceValue;
		}

		bool CommandQueue::IsFenceComplete(uint64_t fenceValue)
		{
			return fence->GetCompletedValue() >= fenceValue;
		}

		void CommandQueue::WaitForFenceValue(uint64_t fenceValue)
		{
			if (!IsFenceComplete(fenceValue))
			{
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				WaitForSingleObject(fenceEvent, DWORD_MAX);
			}
		}

		ComPtr<ID3D12CommandQueue> CommandQueue::GetD3D12CommandQueue() const
		{
			return commandQueue;
		}
		
		void CommandQueue::Flush()
		{
			WaitForFenceValue(Signal());
		}
		
		uint64_t CommandQueue::ExecuteCommandList(ComPtr<ID3D12GraphicsCommandList2> commandList)
		{
			commandList->Close();

			ID3D12CommandAllocator* commandAllocator;
			UINT dataSize = sizeof(commandAllocator);
			
			auto result = (commandList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator));
			CheckComError(result, "Failed to SetPrivateDataInterface");
			
			ID3D12CommandList* const ppCommandLists[] = {
				commandList.Get()
			};

			commandQueue->ExecuteCommandLists(1, ppCommandLists);
			const uint64_t fenceValue = Signal();

			commandAllocatorQueue.emplace(CommandAllocatorEntry{ fenceValue, commandAllocator });
			commandListQueue.push(commandList);

			// The ownership of the command allocator has been transferred to the ComPtr
			// in the command allocator queue. It is safe to release the reference 
			// in this temporary COM pointer here.
			commandAllocator->Release();

			return fenceValue;
		}
		
	}
}

